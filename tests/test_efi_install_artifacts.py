"""Installer failure/identity tests with an actual verified bundle."""
from pathlib import Path
from unittest.mock import patch
import sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import hak_efi_install as installer
from hak_efi_volumes import ESP


def rejects(operation):
    try:operation()
    except (ValueError,OSError):return
    raise AssertionError('Invalid installation accepted')


def run(bundle):
    with tempfile.TemporaryDirectory(prefix='hak-install-') as tmp:
        base=Path(tmp);mount=base/'esp';mount.mkdir()
        other=mount/'EFI/BOOT';other.mkdir(parents=True)
        (other/'BOOTX64.EFI').write_bytes(b'existing loader')
        selected=ESP(42,1048576,67108864,'/dev/disk/virtual/1/raw','/dev/disk/virtual/1/0',str(mount),'test')
        with patch.object(installer,'discover',return_value=[selected]):
            reviewed=installer.review(bundle,Path('helper'),42)
        with patch.object(installer,'open_volume',return_value=mount) as opened:
            changed=dict(reviewed,bundle_sha256='0'*64)
            rejects(lambda:installer.install(bundle,Path('helper'),changed))
            opened.assert_not_called()
            destination=installer.install(bundle,Path('helper'),reviewed)
            assert opened.call_count==2
            assert (destination/'HAKBOOT.EFI').read_bytes()==(bundle/'EFI/HAK/HAKBOOT.EFI').read_bytes()
            assert not (destination/'INSTALL-PENDING.json').exists()
            assert (destination/'INSTALL.json').is_file()
            rejects(lambda:installer.install(bundle,Path('helper'),reviewed))
        assert (other/'BOOTX64.EFI').read_bytes()==b'existing loader'
        missing=base/'missing';missing.mkdir()
        with patch.object(installer,'open_volume',side_effect=ValueError('partition changed')):
            rejects(lambda:installer.install(bundle,Path('helper'),reviewed))
        partial=base/'partial';partial.mkdir();original=installer.write_new
        def fail_image(path,data):
            if path.name=='HAKBOOT.NEW':raise OSError('simulated full filesystem')
            original(path,data)
        with patch.object(installer,'open_volume',return_value=partial),patch.object(installer,'write_new',side_effect=fail_image):
            rejects(lambda:installer.install(bundle,Path('helper'),reviewed))
        assert not (partial/'EFI/HAK/HAKBOOT.EFI').exists()
        assert (partial/'EFI/HAK/INSTALL-PENDING.json').exists()
        changed_mount=base/'changed';changed_mount.mkdir()
        with patch.object(installer,'open_volume',side_effect=[changed_mount,mount]):
            rejects(lambda:installer.install(bundle,Path('helper'),reviewed))
        assert not (changed_mount/'EFI').exists()
    print('PASS installation: reviewed bundle, partition/mount recheck, existing-loader preservation, incomplete image not committed')


if __name__=='__main__':run(Path(sys.argv[1]))
