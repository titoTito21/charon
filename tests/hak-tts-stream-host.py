"""PCM becomes available before synthesis ends; cancellation keeps framing aligned."""
import json,sys,tempfile,threading,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'engine'))
from hakTtsRegistry import Worker
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);worker=root/'worker.py'
    worker.write_text('''import json,sys
for line in sys.stdin:
 r=json.loads(line)
 if 'cancel' in r:continue
 identifier=r['id']
 def emit(data,more):
  sys.stdout.buffer.write((json.dumps(dict(id=identifier,sr=22050,bytes=len(data),more=more))+'\\n').encode()+data);sys.stdout.buffer.flush()
 if r['text']=='interrupt':
  emit(b'\\x01\\x00',True)
  cancel=json.loads(sys.stdin.readline());assert cancel['cancel']==identifier
  emit(b'',False)
 else:
  emit(b'\\x02\\x00',True);emit(b'\\x03\\x00',False)
''')
    client=Worker(dict(worker=str(worker),library=directory))
    try:
        stream=client.stream('interrupt')
        assert next(stream)==b'\x01\0' # worker cannot finish until this caller cancels
        client.interrupt()
        assert list(stream)==[]
        assert client.render('replacement')==b'\x02\0\x03\0'
        sequence=client.sequence
        assert list(client.stream('stale',_cancelled=lambda:True))==[]
        assert client.sequence==sequence
        assert client.render('replacement again')==b'\x02\0\x03\0'
    finally:client.cancel()
print('PASS first PCM before completion, interrupted stream drain, replacement framing and pre-cancel race')
