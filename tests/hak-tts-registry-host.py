"""Registry isolation, malformed records, paths and worker cancellation."""
import json,sys,tempfile,threading,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'engine'))
from hakTtsRegistry import registrations,Worker
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    (root/'lib').mkdir()
    worker=root/'worker.py'
    worker.write_text('import sys,json,time\nfor line in sys.stdin:\n r=json.loads(line)\n if r["text"]=="wait":time.sleep(30)\n sys.stdout.buffer.write(b\'{"sr":22050,"bytes":4}\\n\\x01\\x00\\x02\\x00\');sys.stdout.buffer.flush()\n')
    good=dict(version=1,id='test',name='Test',backend='pcm-worker',worker='worker.py',library='lib',voices=['Adam'],variants={'klatt':['default']})
    (root/'good.json').write_text(json.dumps(good))
    for name,changes in [('escape',dict(worker='../outside.py')),('badvoices',dict(voices=42)),('badvariants',dict(variants={'x':42})),('id',dict(id='../invalid'))]:
        (root/(name+'.json')).write_text(json.dumps(dict(good,**changes)))
    (root/'broken.json').write_text('{broken')
    result=registrations([root]);assert list(result)==['test'],result
    client=Worker(result['test'])
    assert client.render('hello')==b'\x01\0\x02\0'
    errors=[]
    def wait():
        try:client.render('wait')
        except Exception as error:errors.append(type(error).__name__)
    thread=threading.Thread(target=wait);thread.start();time.sleep(.1)
    client.cancel();thread.join(3)
    assert not thread.is_alive() and errors
    try:client.render('restart cancelled worker')
    except RuntimeError:pass
    else:raise AssertionError('Cancelled worker restarted')
print('PASS registry validation, PCM framing and cancellation')
