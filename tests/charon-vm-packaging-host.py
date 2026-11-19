"""Fixture-only packaging validation; never launches QEMU or certifies firmware."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
TOOL=Path(__file__).resolve().parents[1]/'tools/package-charon-vm.py'
spec=importlib.util.spec_from_file_location('packager',TOOL);tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
class PackageTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='charon-pack-fixture-');self.base=Path(self.temp.name)
        values={}
        for key in tool.FILES:
            path=self.base/(key+'.fixture');path.write_bytes(b'fixture-only-'+key.encode());values[key]=path
        for key in ('qemu','assets','licenses'):
            path=self.base/key;path.mkdir();values[key]=path
        (values['qemu']/'qemu-system-x86_64.exe').write_bytes(b'NOT AN EXECUTABLE')
        (values['qemu']/'dependency.dll').write_bytes(b'fixture dependency')
        (values['qemu']/'COPYING').write_text('fixture license')
        (values['licenses']/'COMPONENTS.txt').write_text('fixture component notice')
        names=['welcome','focus','edge','wrap','submenu_open','submenu_close','popUp','popup_close','dialog_open','dialog_close','select','continue']
        names += [f'{kind}-{i}' for kind in ('start','background') for i in range(1,5)]
        for name in names:(values['assets']/(name+'.pcm')).write_bytes(b'\0'*8)
        values.update(output=self.base/'output with spaces'/'charon',validation=self.base/'validation.json')
        self.args=SimpleNamespace(**values)
        self.record={'schema':'charon-vm-validation-1','profile':tool.PROFILE,'checks':dict.fromkeys(tool.CHECKS,True),
                     'disk_contains_exact_components':True,'evidence':'TEST FIXTURE ONLY; no actual VM verification',
                     'sha256':{key:tool.sha(values[key]) for key in tool.FILES}}
        self.record['sha256'].update(assets=tool.tree_hash(tool.tree(values['assets'])),qemu=tool.tree_hash(tool.tree(values['qemu'])))
        self.save()
    def tearDown(self):self.temp.cleanup()
    def save(self):self.args.validation.write_text(json.dumps(self.record))
    def test_full_portable_copy_and_manifest(self):
        output=tool.package(self.args)
        self.assertEqual(tool.tree(output/'qemu'),tool.tree(self.args.qemu))
        manifest=json.loads((output/'manifest.json').read_text())
        for name,digest in manifest['files'].items():
            self.assertFalse(Path(name).is_absolute());self.assertNotIn('..',Path(name).parts)
            self.assertEqual(tool.sha(output/name),digest)
        bat=(output/'Uruchom-Charona.bat').read_text();ps=(output/'Uruchom-Charona.ps1').read_text()
        self.assertIn('"%~dp0Uruchom-Charona.ps1"',bat)
        self.assertIn('$PSScriptRoot',ps);self.assertIn("'-nic','none'",ps);self.assertIn('dsound',ps)
        self.assertIn("'ReadWrite', 'None'",ps);self.assertIn('$lock.Dispose()',ps)
        self.assertNotIn(str(self.base),bat+ps)
        self.assertFalse((output/'state').exists())
        with self.assertRaises(ValueError):tool.package(self.args)
    def test_requires_true_real_checks(self):
        for key in tool.CHECKS:
            self.record['checks'][key]=False;self.save()
            with self.assertRaises(ValueError):tool.package(self.args)
            self.assertFalse(self.args.output.exists());self.record['checks'][key]=True
    def test_input_drift(self):
        self.args.charon.write_bytes(b'changed')
        with self.assertRaises(ValueError):tool.package(self.args)
        self.assertFalse(self.args.output.exists())
    def test_wrong_profile_or_disk_attestation(self):
        self.record['profile']='different';self.save()
        with self.assertRaises(ValueError):tool.package(self.args)
        self.record['profile']=tool.PROFILE;self.record['disk_contains_exact_components']=False;self.save()
        with self.assertRaises(ValueError):tool.package(self.args)
    def test_case_correct_assets_required(self):
        (self.args.assets/'popUp.pcm').rename(self.args.assets/'popup_open.pcm')
        with self.assertRaises(ValueError):tool.package(self.args)
    def test_no_recursive_input_output(self):
        self.args.output=self.args.qemu/'nested-output'
        with self.assertRaises(ValueError):tool.package(self.args)
if __name__=='__main__':unittest.main()
