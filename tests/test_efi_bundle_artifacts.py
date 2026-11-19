"""Exercise bundle assembly against actual built EFI artifacts and speech pack."""
from pathlib import Path
import json,shutil,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from hak_efi_bundle import assemble,verify


def refuses(operation):
    try:operation()
    except (ValueError,FileNotFoundError):return
    raise AssertionError('Invalid bundle accepted')


def run(program,driver,speech):
    with tempfile.TemporaryDirectory(prefix='hak-efi-bundle-') as temp:
        root=Path(temp);output=root/'bundle'
        manifest=assemble(program,driver,speech,output,'pl')
        assert len(manifest['files'])==8
        assert (output/'EFI/HAK/audio-language').read_bytes()==b'pl'
        assert not (output/'EFI/BOOT').exists()
        refuses(lambda:assemble(program,driver,speech,output,'pl'))
        refuses(lambda:assemble(program,driver,speech,root/'invalid-language','de'))
        assert not (root/'invalid-language').exists()
        original=(output/'bundle.json').read_bytes()
        (output/'EFI/HAK/audio-language').write_bytes(b'en')
        refuses(lambda:verify(output))
        (output/'EFI/HAK/audio-language').write_bytes(b'pl')
        extra=output/'EFI/HAK/next.bin';extra.write_bytes(b'not a boot request')
        refuses(lambda:verify(output));extra.unlink()
        data=json.loads(original);data['files']['../outside']=data['files'].pop('EFI/HAK/HAKBOOT.EFI')
        (output/'bundle.json').write_text(json.dumps(data))
        refuses(lambda:verify(output));(output/'bundle.json').write_bytes(original)
        name=next((output/'EFI/HAK/SPEECH').glob('*.pcm'));saved=name.read_bytes()
        name.write_bytes(b'bad!');refuses(lambda:verify(output));name.write_bytes(saved)
        outside=root/'outside.pcm';outside.write_bytes(saved);name.unlink();name.symlink_to(outside)
        refuses(lambda:verify(output));name.unlink();name.write_bytes(saved)
        (output/'bundle.json').unlink();refuses(lambda:verify(output));(output/'bundle.json').write_bytes(original)
        assert verify(output)==manifest
        bad_driver=root/'bad.efi';bad_driver.write_bytes(Path(driver).read_bytes()+b'changed')
        refuses(lambda:assemble(program,bad_driver,speech,root/'bad-driver','pl'))
        assert not (root/'bad-driver').exists()
        pack=root/'pack';shutil.copytree(speech,pack)
        next(pack.glob('*.pcm')).unlink()
        refuses(lambda:assemble(program,driver,pack,root/'incomplete','en'))
        assert not (root/'incomplete').exists()
        english=assemble(program,driver,speech,root/'english','en')
        assert english['language']=='en' and verify(root/'english')==english
    print('PASS actual-artifact bundle: PL/EN, no overwrite, incomplete/corrupt/unlisted/path/symlink/driver refusals')


if __name__=='__main__':run(*map(Path,sys.argv[1:]))
