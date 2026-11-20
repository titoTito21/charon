"""r400: build Charon's optional network driver pack (\\EFI\\HAK\\drivers).

  make-charon-netdrivers.py QEMU_SHARE_DIR OUT_DIR

Takes nothing from the network. Two sources, both shipped with QEMU:
  * edk2-x86_64-code.fd  - the edk2 network stack modules (BSD-2-Clause-Patent):
    written as net-NN-<Module>.efi in the order Charon must start them;
  * efi-<card>.rom       - iPXE's UEFI drivers for network cards (GPL-2.0+):
    written as nic-<card>.efi. The intel driver (efi-e1000e.rom) drives the
    whole Intel gigabit family, the realtek driver (efi-rtl8139.rom) the
    RTL8139/8169/8168 family.
Charon loads nic-* only when the firmware drives no adapter and net-* only
when adapters have no IPv4 service (CharonWsNet.h LoadDrivers).
"""
import lzma,struct,sys
from pathlib import Path

# ---- UEFI firmware volume walk ------------------------------------------
LZMA_GUID=bytes.fromhex('98584eee1439594 29d6edc7bd79403cf'.replace(' ',''))
def sections(data):
    at=0
    while at+4<=len(data):
        size=int.from_bytes(data[at:at+3],'little');kind=data[at+3];head=4
        if size==0xffffff:size=struct.unpack_from('<I',data,at+4)[0];head=8
        if size<head or at+size>len(data):return
        yield kind,data[at+head:at+size],data[at:at+size]
        at=(at+size+3)&~3
def files(volume):
    if volume[40:44]!=b'_FVH':return
    header=struct.unpack_from('<H',volume,48)[0];length=struct.unpack_from('<Q',volume,32)[0]
    ext=struct.unpack_from('<H',volume,52)[0];at=header
    if ext:at=(ext+struct.unpack_from('<I',volume,ext+16)[0]+7)&~7
    end=min(length,len(volume))
    while at+24<=end:
        if volume[at:at+24]==b'\xff'*24:return
        size=int.from_bytes(volume[at+20:at+23],'little');kind=volume[at+18];attrs=volume[at+19];head=24
        if attrs&1:size=struct.unpack_from('<Q',volume,at+24)[0];head=32
        if size<head or at+size>end:return
        yield kind,volume[at+head:at+size]
        at=(at+size+7)&~7
def modules(blob,found):
    """Every (UI name -> PE32 image) below `blob`, which may be a raw flash image."""
    start=0
    while True:
        sig=blob.find(b'_FVH',start)
        if sig<0:break
        volume=blob[sig-40:];start=sig+4
        if sig<40:continue
        for kind,body in files(volume):
            walk(body,found)
def walk(body,found):
    name=None;image=None
    for kind,data,whole in sections(body):
        if kind==0x02 and data[:16]==LZMA_GUID:
            offset=struct.unpack_from('<H',data,16)[0]-4
            try:plain=lzma.decompress(data[offset:],format=lzma.FORMAT_ALONE)
            except lzma.LZMAError:continue
            walk(plain,found)
        elif kind==0x02:
            offset=struct.unpack_from('<H',data,16)[0]-4;walk(data[offset:],found)
        elif kind==0x17:modules(data,found)
        elif kind==0x10:image=data
        elif kind==0x15:name=data.decode('utf-16-le').rstrip('\0')
    if name and image and image[:2]==b'MZ':found.setdefault(name,image)

# ---- EFI 1.1 decompression (the LZ77 + Huffman scheme of option ROMs) ------
class Decoder:
    NC=510;NT=19;CBIT=9;MAXNP=31;TBIT=5
    def __init__(self,data,pbit):
        self.src=data;self.at=0;self.bitbuf=0;self.subbit=0;self.bitcount=0;self.pbit=pbit
        self.left=[0]*(2*self.NC);self.right=[0]*(2*self.NC)
        self.clen=[0]*self.NC;self.ptlen=[0]*32;self.ctable=[0]*4096;self.pttable=[0]*256;self.block=0
        self.fill(32)
    def fill(self,n):
        self.bitbuf=(self.bitbuf<<n)&0xffffffff
        while n>self.bitcount:
            n-=self.bitcount;self.bitbuf|=(self.subbit<<n)&0xffffffff
            if self.at<len(self.src):self.subbit=self.src[self.at];self.at+=1
            else:self.subbit=0
            self.bitcount=8
        self.bitcount-=n;self.bitbuf|=self.subbit>>self.bitcount
        self.subbit&=(1<<self.bitcount)-1
    def bits(self,n):
        out=self.bitbuf>>(32-n) if n else 0;self.fill(n);return out
    def table(self,count,lengths,bits,table):
        counts=[0]*17;start=[0]*18;weight=[0]*17
        for i in range(count):
            if lengths[i]>16:raise ValueError('bad table')
            counts[lengths[i]]+=1
        for i in range(1,17):start[i+1]=(start[i]+(counts[i]<<(16-i)))&0xffff
        if start[17]:raise ValueError('bad table')
        shift=16-bits
        for i in range(1,bits+1):start[i]>>=shift;weight[i]=1<<(bits-i)
        for i in range(bits+1,17):weight[i]=1<<(16-i)
        i=start[bits+1]>>shift
        if i:
            while i!=(1<<bits):table[i]=0;i+=1
        avail=count;mask=1<<(15-bits)
        for ch in range(count):
            length=lengths[ch]
            if not length:continue
            nxt=start[length]+weight[length]
            if length<=bits:
                for i in range(start[length],nxt):table[i]=ch
            else:
                k=start[length];ref=(table,k>>shift)
                for _ in range(length-bits):
                    if ref[0][ref[1]]==0:
                        self.right[avail]=self.left[avail]=0;ref[0][ref[1]]=avail;avail+=1
                    node=ref[0][ref[1]]
                    ref=(self.right,node) if k&mask else (self.left,node)
                    k=(k<<1)&0xffff
                ref[0][ref[1]]=ch
            start[length]=nxt
    def read_pt(self,nn,nbit,special):
        number=self.bits(nbit)
        if not number:
            c=self.bits(nbit)
            for i in range(256):self.pttable[i]=c
            for i in range(nn):self.ptlen[i]=0
            return
        i=0
        while i<number and i<32:
            c=self.bitbuf>>29
            if c==7:
                mask=1<<28
                while mask&self.bitbuf:mask>>=1;c+=1
            self.fill(3 if c<7 else c-3);self.ptlen[i]=c;i+=1
            if i==special:
                for _ in range(self.bits(2)):
                    if i<32:self.ptlen[i]=0;i+=1
        while i<nn and i<32:self.ptlen[i]=0;i+=1
        self.table(nn,self.ptlen,8,self.pttable)
    def read_c(self):
        number=self.bits(self.CBIT)
        if not number:
            c=self.bits(self.CBIT)
            for i in range(self.NC):self.clen[i]=0
            for i in range(4096):self.ctable[i]=c
            return
        i=0
        while i<number and i<self.NC:
            c=self.pttable[self.bitbuf>>24]
            if c>=self.NT:
                mask=1<<23
                while c>=self.NT:
                    c=self.right[c] if self.bitbuf&mask else self.left[c];mask>>=1
            self.fill(self.ptlen[c])
            if c<=2:
                run=1 if c==0 else self.bits(4)+3 if c==1 else self.bits(self.CBIT)+20
                for _ in range(run):
                    if i<self.NC:self.clen[i]=0;i+=1
            else:self.clen[i]=c-2;i+=1
        while i<self.NC:self.clen[i]=0;i+=1
        self.table(self.NC,self.clen,12,self.ctable)
    def decode_c(self):
        if not self.block:
            self.block=self.bits(16);self.read_pt(self.NT,self.TBIT,3);self.read_c();self.read_pt(self.MAXNP,self.pbit,-1)
        self.block-=1;c=self.ctable[self.bitbuf>>20]
        if c>=self.NC:
            mask=1<<19
            while c>=self.NC:
                c=self.right[c] if self.bitbuf&mask else self.left[c];mask>>=1
        self.fill(self.clen[c]);return c
    def decode_p(self):
        v=self.pttable[self.bitbuf>>24]
        if v>=self.MAXNP:
            mask=1<<23
            while v>=self.MAXNP:
                v=self.right[v] if self.bitbuf&mask else self.left[v];mask>>=1
        self.fill(self.ptlen[v])
        return (1<<(v-1))+self.bits(v-1) if v>1 else v
def decompress(data):
    comp,orig=struct.unpack_from('<II',data,0);last=None
    for pbit in (4,5):   # EFI 1.1, then Tiano
        try:
            d=Decoder(data[8:8+comp],pbit);out=bytearray()
            while len(out)<orig:
                c=d.decode_c()
                if c<256:out.append(c)
                else:
                    length=c-253;at=len(out)-d.decode_p()-1
                    if at<0:raise ValueError('bad distance')
                    for _ in range(length):
                        if len(out)>=orig:break
                        out.append(out[at]);at+=1
            if out[:2]==b'MZ':return bytes(out)
            last=ValueError('not a PE image')
        except (ValueError,IndexError) as error:last=error
    raise last
def rom_driver(rom):
    """The x64 UEFI driver inside a PCI option ROM."""
    at=0
    while at+0x1c<=len(rom) and rom[at:at+2]==b'\x55\xaa':
        pcir=at+struct.unpack_from('<H',rom,at+0x18)[0]
        if rom[pcir:pcir+4]!=b'PCIR':break
        length=struct.unpack_from('<H',rom,pcir+0x10)[0]*512;code=rom[pcir+0x14];last=rom[pcir+0x15]&0x80
        if code==3 and struct.unpack_from('<I',rom,at+4)[0]==0x0ef1 and struct.unpack_from('<H',rom,at+0x0a)[0]==0x8664:
            offset=struct.unpack_from('<H',rom,at+0x16)[0];packed=struct.unpack_from('<H',rom,at+0x0c)[0]
            body=rom[at+offset:at+length]
            return decompress(body) if packed else body
        if last or not length:break
        at+=length
    return None

STACK=['DpcDxe','SnpDxe','MnpDxe','ArpDxe','Ip4Dxe','Udp4Dxe','Dhcp4Dxe','TcpDxe','DnsDxe','HttpUtilitiesDxe','HttpDxe']
CARDS={'efi-e1000e.rom':'nic-intel.efi','efi-rtl8139.rom':'nic-realtek.efi','efi-virtio.rom':'nic-virtio.efi',
       'efi-pcnet.rom':'nic-pcnet.efi','efi-eepro100.rom':'nic-eepro100.efi'}
def main():
    share=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=False)
    found={};modules((share/'edk2-x86_64-code.fd').read_bytes(),found)
    for order,name in enumerate(STACK):
        if name not in found:print('missing in firmware image:',name);continue
        target=out/f'net-{10+order}-{name}.efi';target.write_bytes(found[name]);print(target.name,len(found[name]))
    for rom,name in CARDS.items():
        path=share/rom
        if not path.exists():print('no',rom);continue
        image=rom_driver(path.read_bytes())
        if not image:print('no x64 UEFI driver in',rom);continue
        (out/name).write_bytes(image);print(name,len(image))
    (out/'README.txt').write_text(
        'Charon optional network drivers (\\EFI\\HAK\\drivers on any FAT volume).\n'
        'net-*.efi: edk2 NetworkPkg modules, BSD-2-Clause-Patent (see edk2-licenses.txt of QEMU).\n'
        'nic-*.efi: iPXE UEFI network card drivers, GPL-2.0-or-later (https://ipxe.org, source: https://github.com/ipxe/ipxe).\n'
        'wifi-*.efi: put a UEFI Wi-Fi driver of your card here if its maker ships one; Charon loads it.\n'
        'Charon loads nic-*/wifi-* only when the firmware drives no adapter, and net-* only when adapters have no IPv4 service.\n',encoding='utf-8')
if __name__=='__main__':main()
