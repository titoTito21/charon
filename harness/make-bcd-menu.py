"""Test BCD with a boot menu from Windows' own DVD template, without bcdedit (r361).

The DVD template's {bootmgr} lists one entry ("Windows Setup") in displayorder
and Windows Memory Diagnostic in toolsdisplayorder. This copy lists both in
displayorder, so Windows Boot Manager shows its "Choose an operating system"
menu. The new REG_MULTI_SZ data goes into a free cell of the hive; the value
cell is pointed at it. Nothing else changes (header, keys, other values).
  python make-bcd-menu.py SOURCE_BCD OUTPUT_BCD
"""
import struct,sys
SETUP='{7619dcc9-fafe-11d9-b411-000476eba25f}';MEMDIAG='{b2721d73-1db4-4c62-bf78-c548a880142d}'

def cells(h):
    """(offset of cell from hive start, size) for every cell in every hbin."""
    pos=4096
    while pos+32<=len(h) and h[pos:pos+4]==b'hbin':
        size=struct.unpack_from('<I',h,pos+8)[0];cell=pos+32
        while cell<pos+size:
            s=struct.unpack_from('<i',h,cell)[0]
            if s==0:raise ValueError('zero cell size')
            yield cell,s;cell+=abs(s)
        pos+=size

def main(source,output,standard=False):
    h=bytearray(open(source,'rb').read())
    assert h[:4]==b'regf'
    old=(SETUP+'\0\0').encode('utf-16-le')
    new=(SETUP+'\0'+MEMDIAG+'\0\0').encode('utf-16-le')
    target=None
    for cell,s in cells(h):
        if s<0 and h[cell+4:cell+6]==b'vk':
            length,offset,kind=struct.unpack_from('<IIi',h,cell+8)
            if kind==7 and not length&0x80000000 and length==len(old) and h[4096+offset+4:4096+offset+4+length]==old:
                assert target is None,'displayorder found twice';target=cell
    assert target,'displayorder of the DVD template not found'
    need=(4+len(new)+7)&~7
    for cell,s in cells(h):
        if s>=need:
            rest=s-need
            if rest and rest<8:continue
            struct.pack_into('<i',h,cell,-need);h[cell+4:cell+4+len(new)]=new
            if rest:struct.pack_into('<i',h,cell+need,rest)
            struct.pack_into('<II',h,target+8,len(new),cell-4096)
            break
    else:raise ValueError('no free cell for the new displayorder')
    # bootmenupolicy (element 250000c2 of Windows Setup): Standard (1) lets the
    # Windows loader draw the menu after it starts; Legacy (0) makes Windows
    # Boot Manager draw it itself. Same 8 data bytes, first one cleared.
    patched=0
    if standard:
        open(output,'wb').write(h);print('displayorder: Windows Setup, Windows Memory Diagnostic; bootmenupolicy Standard (unchanged) ->',output);return
    for cell,s in cells(h):
        if s<0 and h[cell+4:cell+6]==b'nk':
            nameLength=struct.unpack_from('<H',h,cell+4+0x48)[0]
            if h[cell+4+0x4c:cell+4+0x4c+nameLength]!=b'250000c2':continue
            count,listOffset=struct.unpack_from('<II',h,cell+4+0x24)
            for i in range(count):
                vk=4096+struct.unpack_from('<I',h,4096+listOffset+4+4*i)[0]
                length,offset,kind=struct.unpack_from('<IIi',h,vk+8)
                assert h[vk+4:vk+6]==b'vk' and kind==3 and length==8,'unexpected bootmenupolicy value'
                assert h[4096+offset+4]==1;h[4096+offset+4]=0;patched+=1
    assert patched==1,patched
    open(output,'wb').write(h)
    print('displayorder: Windows Setup, Windows Memory Diagnostic; bootmenupolicy Legacy ->',output)

if __name__=='__main__':main(sys.argv[1],sys.argv[2],len(sys.argv)>3 and sys.argv[3]=='standard')
