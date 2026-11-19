#define efi_main unused_component_entry
#include "../src/efi/HAKBootEFI.cpp"
#undef efi_main
extern "C" efi_status EFIAPI efi_main(efi_handle self,efi_system_table* table) {
    efi_loaded_image_protocol* loaded=nullptr;
    if(table->BootServices->HandleProtocol(self,&imageGuid,(void**)&loaded)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    if(loaded->LoadOptionsSize!=2 || !loaded->LoadOptions)return EFI_INVALID_PARAMETER;
    auto* options=(uint8_t*)loaded->LoadOptions;
    if(options[0]!=0x48 || options[1]!=0x41)return EFI_INVALID_PARAMETER;
    efi_guid guid={0x48616b11,0x8412,0x4610,{0xab,0x11,0x43,0x13,0x15,0x71,0x33,0x02}};
    uint8_t reached=1;
    return table->RuntimeServices->SetVariable((char16_t*)u"HakFixtureReached",&guid,EFI_VARIABLE_BOOTSERVICE_ACCESS,1,&reached);
}
