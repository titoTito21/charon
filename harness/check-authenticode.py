"""r385: independent check of an Authenticode-signed UEFI image (Charon add-on).

  check-authenticode.py IMAGE.efi CERT.cer

Recomputes the PE/COFF Authenticode SHA-256, parses the WIN_CERTIFICATE's
PKCS#7 by hand (DER), and checks: the SpcIndirectDataContent digest equals
the image hash, messageDigest equals SHA-256 over the content octets (the
SEQUENCE's contents, as edk2 AuthenticodeVerify passes them), and the RSA
signature over the signed attributes verifies with CERT.
"""
import hashlib,struct,sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding

def tlv(b,i):
    tag=b[i];n=b[i+1];j=i+2
    if n&0x80:k=n&0x7f;n=int.from_bytes(b[j:j+k],'big');j+=k
    return tag,j,j+n          # tag, content start, end
def children(b,start,end):
    out=[];i=start
    while i<end:t,s,e=tlv(b,i);out.append((t,s,e,i));i=e
    return out

img=open(sys.argv[1],'rb').read();cert=x509.load_der_x509_certificate(open(sys.argv[2],'rb').read())
pe=struct.unpack_from('<I',img,0x3c)[0];coff=pe+4;nsec,=struct.unpack_from('<H',img,coff+2);optsz,=struct.unpack_from('<H',img,coff+16)
opt=coff+20;chk=opt+64;dirent=opt+112+32;hdrs,=struct.unpack_from('<I',img,opt+60);coff_c,csize=struct.unpack_from('<II',img,dirent)
secs=sorted((struct.unpack_from('<I',img,opt+optsz+40*i+20)[0],struct.unpack_from('<I',img,opt+optsz+40*i+16)[0]) for i in range(nsec))
h=hashlib.sha256();h.update(img[:chk]);h.update(img[chk+4:dirent]);h.update(img[dirent+8:hdrs]);done=hdrs
for off,size in secs:
    if size:h.update(img[off:off+size]);done+=size
if coff_c>done:h.update(img[done:coff_c])
digest=h.digest()
assert coff_c+csize==len(img) and coff_c%8==0,'certificate table placement'
length,rev,typ=struct.unpack_from('<IHH',img,coff_c);assert rev==0x200 and typ==2 and length==csize,(rev,typ,length,csize)
p7=img[coff_c+8:coff_c+length]
t,s,e=tlv(p7,0);ci=children(p7,s,e)                 # ContentInfo: oid, [0]
_,s0,e0=ci[1][0:3];t,s1,e1=tlv(p7,s0);sd=children(p7,s1,e1)   # SignedData fields
encap=sd[2];ec=children(p7,encap[1],encap[2])        # contentType, [0] content
assert p7[ec[0][1]:ec[0][2]].hex()=='2b060104018237020104','inner content type is not SpcIndirectDataContent'
t,c0,c1=tlv(p7,ec[1][1])
wrapped=t==0x04
if wrapped:t,c0,c1=tlv(p7,c0)
assert t==0x30
spc_contents=p7[c0:c1]
spc=children(p7,c0,c1);di=children(p7,spc[1][1],spc[1][2]);inner=p7[di[1][1]:di[1][2]]
print('image hash   ',digest.hex());print('spc digest   ',inner.hex(),'(content OCTET STRING wrapped)' if wrapped else '(PKCS#7 v1.5 content)')
assert inner==digest,'SpcIndirectDataContent digest differs from the image hash'
signer=children(p7,sd[-1][1],sd[-1][2])[0];si=children(p7,signer[1],signer[2])
attrs=next(x for x in si if x[0]==0xa0)
md=None
for a in children(p7,attrs[1],attrs[2]):
    f=children(p7,a[1],a[2])
    if p7[f[0][1]:f[0][2]].hex()=='2a864886f70d010904':v=children(p7,f[1][1],f[1][2])[0];md=p7[v[1]:v[2]]
assert md==hashlib.sha256(spc_contents).digest(),'messageDigest is not SHA-256 over the content octets'
sig=next(x for x in si if x[0]==0x04)
signed=b'\x31'+p7[attrs[3]+1:attrs[2]]
cert.public_key().verify(p7[sig[1]:sig[2]],signed,padding.PKCS1v15(),hashes.SHA256())
print('PASS Authenticode: image hash, messageDigest and signature verify with',cert.subject.rfc4514_string())
