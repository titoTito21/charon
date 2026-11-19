import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from hak_efi_speech_pack import message_name,convert,export
import hashlib,json,struct,tempfile,unittest

class SpeechPackTest(unittest.TestCase):
    def test_identity_and_samples(self):
        self.assertEqual(message_name('pl','Menedżer rozruchu Haiku Accessibility Kit.'),'0a0f1984fa661905.pcm')
        self.assertEqual(message_name('en','HAK boot manager.'),'8320455899e69218.pcm')
        data=convert(struct.pack('<hhh',-32768,0,32767))
        samples=struct.unpack('<'+'h'*(len(data)//2),data)
        self.assertEqual(samples[::2],samples[1::2])
        self.assertEqual(samples[0],-32768)
        for value in (b'',b'x'):
            with self.assertRaises(ValueError):convert(value)

    def test_verified_language_export_and_refusals(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);cache=root/'cache';cache.mkdir()
            records={}
            for lang,text in [('pl','Próba'),('en','Test')]:
                name=message_name(lang,text);data=struct.pack('<hh',100,-100)
                (cache/name).write_bytes(data)
                records[name]=dict(language=lang,text=text,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
            (cache/'manifest.json').write_text(json.dumps(records),encoding='utf-8')
            result=export(cache,root/'output',['pl','en'])
            self.assertEqual(len(result['messages']),2)
            for name,r in result['messages'].items():
                self.assertEqual(hashlib.sha256((root/'output'/name).read_bytes()).hexdigest(),r['sha256'])
            with self.assertRaises(ValueError):export(cache,root/'output',['pl'])
            with self.assertRaises(ValueError):export(cache,root/'missing',['de'])
            name=next(iter(records));(cache/name).write_bytes(b'bad!')
            with self.assertRaises(ValueError):export(cache,root/'corrupt',['pl'])
            self.assertFalse((root/'corrupt').exists())

if __name__=='__main__':unittest.main()
