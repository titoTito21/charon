// Disposable firmware fixture only, after its throwaway PK enrollment.
static bool TestSecureBootPreference() {
    uint8_t initial[hakefi::kSecureControlSize],state[hakefi::kSecureControlSize];
    SnapshotSecureControl(initial);
    if(!hakefi::SecureControlAvailable(initial,sizeof(initial))) {
        Say(u"FAIL: EDK2 preference unavailable after test PK enrollment.\r\n");return false;
    }
    auto* runtime=systemTable->RuntimeServices;uint8_t pkBefore[8192],pkAfter[8192];
    size_t beforeSize=sizeof(pkBefore),afterSize=sizeof(pkAfter);uint32_t beforeAttributes=0,afterAttributes=0;
    if(runtime->GetVariable((char16_t*)u"PK",&globalGuid,&beforeAttributes,&beforeSize,pkBefore)!=EFI_SUCCESS)return false;
    const uint8_t changed=uint8_t(1-initial[20]);
    bool ok=SetEdk2SecureBootPreference(initial,sizeof(initial),changed)==EFI_SUCCESS;
    SnapshotSecureControl(state);
    ok=ok && state[20]==changed && SetEdk2SecureBootPreference(initial,sizeof(initial),initial[20])==EFI_MEDIA_CHANGED;
    // Restore even if a previous assertion failed. This VM and PK are disposable.
    const efi_status restored=SetEdk2SecureBootPreference(state,sizeof(state),initial[20]);
    SnapshotSecureControl(state);
    ok=ok && restored==EFI_SUCCESS && state[20]==initial[20]
        && runtime->GetVariable((char16_t*)u"PK",&globalGuid,&afterAttributes,&afterSize,pkAfter)==EFI_SUCCESS
        && beforeSize==afterSize && beforeAttributes==afterAttributes && hakefi::Same(pkBefore,pkAfter,beforeSize);
    Say(ok ? u"PASS: EDK2 preference off/on readback, stale refusal, PK unchanged; effective mode needs reset.\r\n"
        : u"FAIL: EDK2 preference writes or restoration.\r\n");
    return ok;
}
