"""Generate src/efi/CharonTrustAnchors.h from src/efi/third_party/trust-anchors/roots.pem (r401).

The header holds, for every root certificate, only what BearSSL's X.509
engine needs: the encoded subject name and the public key (RSA modulus and
exponent, or the EC curve and point). Certificates themselves are not
embedded; roots.pem stays in the tree as the auditable origin.

Usage:
  python gen-charon-trust-anchors.py                 regenerate the header from roots.pem
  python gen-charon-trust-anchors.py --select BUNDLE first rewrite roots.pem from a PEM bundle
                                                     (e.g. Mozilla's ca-bundle.crt), keeping
                                                     the certificates named in WANTED
No third-party modules; a minimal DER reader is enough.
"""
from pathlib import Path
import base64,hashlib,re,sys

root=Path(__file__).resolve().parents[1]
pemPath=root/'src/efi/third_party/trust-anchors/roots.pem'
headerPath=root/'src/efi/CharonTrustAnchors.h'
# Common names of the roots Charon trusts, with why.
WANTED=[
 ('DigiCert Global Root G2','www.microsoft.com, vlscppe, ov-df, software.download.prss.microsoft.com (via Microsoft TLS RSA Root G2)'),
 ('DigiCert Global Root G3','software-download.microsoft.com, download.microsoft.com (via Microsoft TLS ECC Root G2)'),
 ('Microsoft RSA Root Certificate Authority 2017','Microsoft hosts that chain to the own root'),
 ('Microsoft ECC Root Certificate Authority 2017','Microsoft hosts that chain to the own root'),
 ('ISRG Root X1',"Let's Encrypt: deb.debian.org, cdimage.debian.org, releases.ubuntu.com"),
 ('ISRG Root X2',"Let's Encrypt ECDSA chains"),
 ('USERTrust RSA Certification Authority','Sectigo: many distribution mirrors'),
 ('USERTrust ECC Certification Authority','Sectigo ECDSA chains'),
 ('GTS Root R1','Google Trust Services: mirrors behind Google and Cloudflare'),
 ('GTS Root R4','Google Trust Services ECDSA chains'),
 ('Amazon Root CA 1','hosts behind CloudFront and S3'),
]
def tlv(data,at):
    tag=data[at];length=data[at+1];head=2
    if length&0x80:
        count=length&0x7f;length=int.from_bytes(data[at+2:at+2+count],'big');head=2+count
    return tag,at+head,at+head+length   # tag, content start, content end (= next element)
def children(data,start,end):
    out=[]
    while start<end:
        tag,a,b=tlv(data,start);out.append((tag,start,a,b));start=b
    return out
OID_CN=bytes([0x55,0x04,0x03])
CURVES={bytes.fromhex('2a8648ce3d030107'):(23,'BR_EC_secp256r1'),bytes.fromhex('2b81040022'):(24,'BR_EC_secp384r1'),bytes.fromhex('2b81040023'):(25,'BR_EC_secp521r1')}
OID_RSA=bytes.fromhex('2a864886f70d010101');OID_EC=bytes.fromhex('2a8648ce3d0201')
def parse(der):
    _,a,b=tlv(der,0);top=children(der,a,b);tbs=children(der,top[0][2],top[0][3])
    index=1 if tbs[0][0]==0xa0 else 0
    issuer=tbs[index+2];validity=tbs[index+3];subject=tbs[index+4];spki=tbs[index+5]
    dn=der[subject[1]:subject[3]]
    name=None
    for rdn in children(der,subject[2],subject[3]):
        for pair in children(der,rdn[2],rdn[3]):
            parts=children(der,pair[2],pair[3])
            if der[parts[0][2]:parts[0][3]]==OID_CN:name=der[parts[1][2]:parts[1][3]].decode('utf-8','replace')
    times=[der[t[2]:t[3]].decode() for t in children(der,validity[2],validity[3])]
    parts=children(der,spki[2],spki[3]);algorithm=children(der,parts[0][2],parts[0][3])
    oid=der[algorithm[0][2]:algorithm[0][3]];bits=der[parts[1][2]+1:parts[1][3]]
    selfSigned=der[issuer[1]:issuer[3]]==dn
    if oid==OID_RSA:
        _,a,b=tlv(bits,0);numbers=children(bits,a,b)
        n=bits[numbers[0][2]:numbers[0][3]].lstrip(b'\0');e=bits[numbers[1][2]:numbers[1][3]].lstrip(b'\0')
        key=('rsa',n,e)
    elif oid==OID_EC:
        key=('ec',CURVES[der[algorithm[1][2]:algorithm[1][3]]],bits)
    else:raise SystemExit('unsupported key type in '+str(name))
    return {'name':name,'dn':dn,'key':key,'notAfter':times[1],'selfSigned':selfSigned,'sha256':hashlib.sha256(der).hexdigest()}
def pems(text):
    return [base64.b64decode(''.join(block.split())) for block in re.findall(r'-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----',text,re.S)]
if '--all' in sys.argv:
    # r402 (the text web browser): every root of the bundle (Mozilla's list as curl ships it), in the bundle's order.
    bundle=Path(sys.argv[sys.argv.index('--all')+1]).read_text(encoding='utf-8',errors='replace')
    kept=[]
    for der in pems(bundle):
        try:info=parse(der)
        except (Exception,SystemExit):continue
        if info['selfSigned']:kept.append((info['name'] or 'unnamed root',der))
    pemPath.parent.mkdir(parents=True,exist_ok=True)
    with pemPath.open('w',encoding='ascii',newline='\n') as out:
        for name,der in kept:
            body=base64.b64encode(der).decode()
            out.write('# '+name.encode('ascii','replace').decode()+'\n# SHA-256 '+hashlib.sha256(der).hexdigest()+'\n-----BEGIN CERTIFICATE-----\n')
            out.write('\n'.join(body[i:i+64] for i in range(0,len(body),64))+'\n-----END CERTIFICATE-----\n\n')
elif '--select' in sys.argv:
    bundle=Path(sys.argv[sys.argv.index('--select')+1]).read_text(encoding='utf-8',errors='replace')
    found={}
    for der in pems(bundle):
        try:info=parse(der)
        except Exception:continue
        if info['selfSigned'] and info['name'] in dict(WANTED):found[info['name']]=der
    missing=[name for name,_ in WANTED if name not in found]
    if missing:raise SystemExit('not in the bundle: '+', '.join(missing))
    pemPath.parent.mkdir(parents=True,exist_ok=True)
    with pemPath.open('w',encoding='ascii',newline='\n') as out:
        for name,_ in WANTED:
            body=base64.b64encode(found[name]).decode()
            out.write('# '+name+'\n# SHA-256 '+hashlib.sha256(found[name]).hexdigest()+'\n-----BEGIN CERTIFICATE-----\n')
            out.write('\n'.join(body[i:i+64] for i in range(0,len(body),64))+'\n-----END CERTIFICATE-----\n\n')
anchors=[parse(der) for der in pems(pemPath.read_text(encoding='ascii'))]
for a in anchors:a['name']=(a['name'] or 'unnamed root').encode('ascii','replace').decode().replace(chr(92),'/').replace(chr(34),chr(39))
assert anchors and all(a['selfSigned'] for a in anchors)
def array(name,data):
    rows=[','.join('0x%02x'%b for b in data[i:i+24]) for i in range(0,len(data),24)]
    return 'inline const unsigned char '+name+'[]={\n '+',\n '.join(rows)+'};\n'
why=dict(WANTED)
lines=['#pragma once\n','// GENERATED by tools/gen-charon-trust-anchors.py from src/efi/third_party/trust-anchors/roots.pem.\n',
 '// Root certificates Charon\'s HTTPS client (CharonHttps.h) trusts: subject name and public key only.\n',
 '// Do not edit; change WANTED in the generator and regenerate.\n','#include <stddef.h>\n','namespace hakefi {\nnamespace https {\nnamespace anchors {\n',
 '// kind: 1 RSA (a = modulus, b = public exponent), 2 EC (a = point, curve = BearSSL curve identifier).\n',
 'struct Root {const char* name;const unsigned char* dn;size_t dnSize;unsigned char kind;int curve;const unsigned char* a;size_t aSize;const unsigned char* b;size_t bSize;};\n']
table=[]
for i,a in enumerate(anchors):
    lines.append('// %s\n//   SHA-256 of the certificate %s, valid until %s\n//   %s\n'%(a['name'],a['sha256'],a['notAfter'],why.get(a['name'],'')))
    lines.append(array('dn%d'%i,a['dn']))
    if a['key'][0]=='rsa':
        lines.append(array('n%d'%i,a['key'][1]));lines.append(array('e%d'%i,a['key'][2]))
        table.append(' {"%s",dn%d,sizeof(dn%d),1,0,n%d,sizeof(n%d),e%d,sizeof(e%d)}'%(a['name'],i,i,i,i,i,i))
    else:
        lines.append(array('q%d'%i,a['key'][2]))
        table.append(' {"%s",dn%d,sizeof(dn%d),2,%d,q%d,sizeof(q%d),nullptr,0}'%(a['name'],i,i,a['key'][1][0],i,i))
lines.append('constexpr size_t rootCount=%d;\n'%len(anchors))
lines.append('inline const Root roots[rootCount]={\n'+',\n'.join(table)+'};\n')
lines.append('}  // namespace anchors\n}  // namespace https\n}  // namespace hakefi\n')
headerPath.write_text(''.join(lines),encoding='ascii',newline='\n')
print('PASS %d trust anchors -> %s (%d bytes of key and name data)'%(len(anchors),headerPath,sum(len(a['dn'])+len(a['key'][1] if a['key'][0]=='rsa' else a['key'][2])+3 for a in anchors)))
for a in anchors:print(' ',a['name'],a['key'][0],a['sha256'][:16],a['notAfter'])
