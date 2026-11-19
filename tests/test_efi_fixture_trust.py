import hashlib
import struct
import unittest
from efi_fixture_trust import fixture_digest


class FixtureTrustTest(unittest.TestCase):
    def image(self):
        data=bytearray(1040)
        data[:2]=b'MZ';struct.pack_into('<I',data,60,128)
        data[128:132]=b'PE\0\0'
        struct.pack_into('<HH',data,132,0x8664,1)
        struct.pack_into('<H',data,148,240)
        struct.pack_into('<H',data,152,0x20b)
        struct.pack_into('<I',data,212,512)
        struct.pack_into('<I',data,260,16)
        struct.pack_into('<II',data,408,512,512)
        data[512:]=b'x'*528
        return data

    def test_exclusions_sections_and_overlay(self):
        data=self.image()
        expected=hashlib.sha256(data[:216]+data[220:296]+data[304:]).digest()
        self.assertEqual(fixture_digest(data),expected)
        data[216:220]=b'abcd'
        self.assertEqual(fixture_digest(data),expected)
        data[-1]^=1
        self.assertNotEqual(fixture_digest(data),expected)

    def test_refuse_ambiguous_or_signed_layout(self):
        for offset,value in ((412,513),(296,1024),(212,300),(132,0)):
            data=self.image();struct.pack_into('<I',data,offset,value)
            with self.assertRaises(ValueError):fixture_digest(data)
        with self.assertRaises(ValueError):fixture_digest(self.image()[:900])


if __name__=='__main__':unittest.main()
