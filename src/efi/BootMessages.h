#pragma once
#include <stdint.h>
#include <stddef.h>
namespace hakefi {
enum class BootProblem {emptyInventory, inventoryRead, continueMissing, continueInvalid, continueRead,
    targetChanged, noSelection, audioStop, loadMissing, security, denied, loadFormat, unsupported,
    corrupted, noMedia, mediaChanged, loadFailed, imageProtocol, loaderReturned, nothingToStart /* r396 */};
enum class BootAttemptStage {validation, load, protocol, returned};
// UEFI status codes, independent of host pointer width. Never infer a signature
// failure from ACCESS_DENIED, nor physical disk absence from empty BootOrder.
inline BootProblem AttemptProblem(BootAttemptStage stage,uint64_t status) {
    if(stage==BootAttemptStage::validation)return BootProblem::targetChanged;
    if(stage==BootAttemptStage::returned)return BootProblem::loaderReturned;
    if(stage==BootAttemptStage::protocol)return BootProblem::imageProtocol;
    const uint64_t error=uint64_t(1)<<63;
    if(status==(error|14))return BootProblem::loadMissing;
    if(status==(error|26))return BootProblem::security;
    if(status==(error|15))return BootProblem::denied;
    if(status==(error|1))return BootProblem::loadFormat;
    if(status==(error|3))return BootProblem::unsupported;
    if(status==(error|10) || status==(error|27) || status==(error|33))return BootProblem::corrupted;
    if(status==(error|12))return BootProblem::noMedia;
    if(status==(error|13))return BootProblem::mediaChanged;
    return BootProblem::loadFailed;
}
inline const char16_t* BootMessage(BootProblem problem,bool pl,bool explicitMessages=false) {
    if(explicitMessages) {switch(problem) {
        case BootProblem::emptyInventory:return pl?u"Nie ma tu ani jednego pieprzonego poprawnego wpisu rozruchowego UEFI poza Charon. To nie znaczy, że nie ma dysków. Sprawdź wpisy firmware i odśwież listę.":u"Not one damn valid UEFI boot entry besides Charon. That does not mean there are no disks. Check the firmware entries and refresh the list.";
        case BootProblem::inventoryRead:return pl?u"Do kurwy nędzy, nie mogę odczytać poprawnej listy rozruchowej UEFI. Odśwież ją albo sprawdź ustawienia firmware.":u"What a clusterfuck. Cannot read a valid UEFI boot list. Refresh it or check the firmware settings.";
        case BootProblem::nothingToStart:return pl?u"Kurwa, nie ma tu żadnego systemu do odpalenia: w kolejności rozruchu ani na dyskach nic poza Charonem i firmware. Podłącz nośnik albo wybierz urządzenie w menu, geniuszu.":u"Fuck, there is no system to start here: nothing in the boot order or on the disks but Charon and firmware. Plug in a medium or pick a device in the menu, genius.";
        case BootProblem::continueMissing:return pl?u"Nie ma tego pierdolonego pliku celu Kontynuuj. Wybierz urządzenie i zapisz cel ponownie. Żaden inny system nie został uruchomiony.":u"The fucking saved Continue target file is missing. Select a device and save it again. No other system was started.";
        case BootProblem::continueInvalid:return pl?u"Ten zapis celu Kontynuuj jest do dupy: nieprawidłowe dane. Wybierz urządzenie i zapisz cel jeszcze raz.":u"Bollocks, this saved Continue target is invalid. Select a device and save the target again.";
        case BootProblem::continueRead:return pl?u"Do cholery, nie da się odczytać zapisanego celu Kontynuuj. Sprawdź nośnik z konfiguracją.":u"Cannot read the damn saved Continue target. Check the configuration storage.";
        case BootProblem::targetChanged:return pl?u"Co jest, kurwa, z tym wpisem? Zniknął, zmienił się albo nie można go potwierdzić. Odśwież listę i wybierz cel ponownie.":u"What the fuck happened to this entry? It is missing, changed, or cannot be verified. Refresh the list and select the target again.";
        case BootProblem::noSelection:return pl?u"Nie wybrano żadnego jebanego celu rozruchowego. Otwórz Urządzenia rozruchowe i wybierz wpis.":u"No fucking boot target is selected. Open Boot devices and select an entry.";
        case BootProblem::audioStop:return pl?u"Ten cholerny dźwięk nie daje się zatrzymać. Przekazanie sterowania zablokowane. Spróbuj ponownie.":u"The damn audio will not stop. Handoff is blocked. Try again.";
        case BootProblem::loadMissing:return pl?u"Do chuja, gdzie jest ten jebany plik rozruchowy? Firmware nie znalazło go pod wybraną ścieżką. Sprawdź ścieżkę, nośnik i wpis rozruchowy.":u"The fucking boot file was not found at the selected path. Check the path, media, and boot entry.";
        case BootProblem::security:return pl?u"Firmware odrzuciło ten jebany plik: naruszenie zabezpieczeń. Sprawdź podpis, zaufane klucze i zasady Secure Boot, do chuja!":u"For fuck’s sake. The firmware rejected this fucking file: security violation. Check its signature, trusted keys, and Secure Boot policy.";
        case BootProblem::denied:return pl?u"Firmware nie daje dostępu do tego pieprzonego programu. Sprawdź zasady dostępu. To nie jest dowód błędu podpisu.":u"Firmware denied access to this fucking program. Check access policy. That is not proof of a signature failure.";
        case BootProblem::loadFormat:return pl?u"Ten cholerny program nie daje się załadować: firmware zgłosiło błąd ładowania. Sprawdź plik rozruchowy.":u"The damn program would not load: firmware reported a loading error. Check the boot file.";
        case BootProblem::unsupported:return pl?u"Co za kurewski problem ze zgodnością. Firmware nie obsługuje tego programu lub sposobu ładowania. Sprawdź zgodność z UEFI.":u"What a fucking compatibility problem. Firmware does not support this program or loading method. Check UEFI compatibility.";
        case BootProblem::corrupted:return pl?u"No pięknie, rozpierdol w danych. Firmware zgłosiło uszkodzenie lub błąd integralności. Sprawdź nośnik i plik rozruchowy.":u"What a shitshow. Firmware reported corrupted data or an integrity error. Check the media and boot file.";
        case BootProblem::noMedia:return pl?u"Do chuja wafla, firmware zgłosiło brak nośnika dla tego celu. Włóż go i odśwież listę.":u"Bollocks. Firmware reported no media for this target. Insert it and refresh the list.";
        case BootProblem::mediaChanged:return pl?u"Ten cholerny nośnik się zmienił, według firmware. Odśwież listę i wybierz cel ponownie.":u"Bloody hell, the media changed, according to firmware. Refresh the list and select the target again.";
        case BootProblem::loadFailed:return pl?u"Chujnia z grzybnią. Firmware nie załadowało programu. Sprawdź kod stanu, nośnik i wpis rozruchowy.":u"What fresh fuckery is this? Firmware did not load the program. Check its status code, media, and boot entry.";
        case BootProblem::imageProtocol:return pl?u"Program załadowany, ale jego cholernego interfejsu obrazu UEFI nie uzyskano. Nie przekazano sterowania. Sprawdź program i kod stanu.":u"The program loaded, but its damn UEFI loaded-image interface was unavailable. Control was not transferred. Check the program and status code.";
        case BootProblem::loaderReturned:return pl?u"No i dupa, program rozruchowy oddał sterowanie do Charon. Sprawdź jego komunikaty i kod stanu.":u"Well, shit, the boot program returned control to Charon. Check its messages and status code.";
    }}

    switch(problem) {
        case BootProblem::emptyInventory:return pl?u"Brak poprawnych wpisów rozruchowych UEFI innych niż Charon. To nie oznacza braku dysków. Sprawdź wpisy w firmware i odśwież listę.":u"No valid UEFI boot entries other than Charon. This does not mean disks are absent. Check firmware boot entries and refresh the list.";
        case BootProblem::inventoryRead:return pl?u"Nie można odczytać poprawnej listy rozruchowej UEFI. Odśwież listę lub sprawdź ustawienia firmware.":u"Cannot read a valid UEFI boot list. Refresh the list or check firmware settings.";
        case BootProblem::nothingToStart:return pl?u"Nie znaleziono systemu do uruchomienia: w kolejności rozruchu i na dyskach jest tylko Charon i programy firmware. Podłącz nośnik albo wybierz urządzenie w menu.":u"No system to start was found: the boot order and the disks hold only Charon and firmware programs. Plug in a medium or select a device in the menu.";
        case BootProblem::continueMissing:return pl?u"Brak pliku zapisanego celu Kontynuuj. Wybierz urządzenie i zapisz cel ponownie. Nie uruchomiono innego systemu.":u"The saved Continue target file is missing. Select a device and save the target again. No other system was started.";
        case BootProblem::continueInvalid:return pl?u"Zapisany cel Kontynuuj jest nieprawidłowy. Wybierz urządzenie i zapisz cel ponownie.":u"The saved Continue target is invalid. Select a device and save the target again.";
        case BootProblem::continueRead:return pl?u"Nie można odczytać zapisanego celu Kontynuuj. Sprawdź nośnik z konfiguracją.":u"Cannot read the saved Continue target. Check the configuration storage.";
        case BootProblem::targetChanged:return pl?u"Wpis celu zniknął, zmienił się lub nie można go potwierdzić. Odśwież listę i wybierz cel ponownie.":u"The target entry is missing, changed, or cannot be verified. Refresh the list and select it again.";
        case BootProblem::noSelection:return pl?u"Brak wybranego celu rozruchowego. Otwórz Urządzenia rozruchowe i wybierz wpis.":u"No boot target is selected. Open Boot devices and select an entry.";
        case BootProblem::audioStop:return pl?u"Nie można zatrzymać dźwięku. Przekazanie sterowania zablokowane; spróbuj ponownie.":u"Audio could not be stopped. Handoff is blocked; try again.";
        case BootProblem::loadMissing:return pl?u"Firmware nie znalazło programu rozruchowego pod wybraną ścieżką. Sprawdź nośnik i wpis rozruchowy.":u"Firmware could not find the boot program at the selected path. Check the media and boot entry.";
        case BootProblem::security:return pl?u"Firmware odmówiło załadowania programu: naruszenie zabezpieczeń. Sprawdź podpis, zaufane klucze i zasady Secure Boot.":u"Firmware refused to load the program: security violation. Check its signature, trusted keys, and Secure Boot policy.";
        case BootProblem::denied:return pl?u"Firmware odmówiło dostępu do programu rozruchowego. Sprawdź zasady dostępu w firmware. To nie potwierdza błędu podpisu.":u"Firmware denied access to the boot program. Check firmware access policy. This does not establish a signature failure.";
        case BootProblem::loadFormat:return pl?u"Firmware zgłosiło błąd ładowania programu. Sprawdź poprawność pliku rozruchowego.":u"Firmware reported a program loading error. Check the boot file.";
        case BootProblem::unsupported:return pl?u"Firmware nie obsługuje wybranego programu lub sposobu ładowania. Sprawdź zgodność programu z UEFI.":u"Firmware does not support the selected program or loading method. Check its UEFI compatibility.";
        case BootProblem::corrupted:return pl?u"Firmware zgłosiło uszkodzenie lub błąd integralności danych. Sprawdź nośnik i plik rozruchowy.":u"Firmware reported corrupted data or an integrity error. Check the media and boot file.";
        case BootProblem::noMedia:return pl?u"Firmware zgłosiło brak nośnika dla wybranego celu. Włóż nośnik i odśwież listę.":u"Firmware reported no media for the selected target. Insert the media and refresh the list.";
        case BootProblem::mediaChanged:return pl?u"Firmware zgłosiło zmianę nośnika. Odśwież listę i wybierz cel ponownie.":u"Firmware reported that the media changed. Refresh the list and select the target again.";
        case BootProblem::loadFailed:return pl?u"Firmware nie załadowało programu. Sprawdź podany kod stanu, nośnik i wpis rozruchowy.":u"Firmware did not load the program. Check the reported status code, media, and boot entry.";
        case BootProblem::imageProtocol:return pl?u"Program załadowano, lecz nie uzyskano jego interfejsu obrazu UEFI. Nie przekazano sterowania.":u"The program loaded, but its UEFI loaded-image interface was unavailable. Control was not transferred.";
        case BootProblem::loaderReturned:return pl?u"Program rozruchowy zwrócił sterowanie do Charon. Sprawdź jego komunikaty i podany kod stanu.":u"The boot program returned control to Charon. Check its messages and the reported status code.";
    }
    return u"";
}
inline void AppendBootStatus(char16_t* text,size_t capacity,uint64_t status) {
    size_t at=0;while(at<capacity && text[at])++at;
    const char16_t prefix[]=u" UEFI: 0x";
    if(at+27>=capacity)return;
    for(size_t i=0;prefix[i];++i)text[at++]=prefix[i];
    const char16_t digits[]=u"0123456789ABCDEF";
    for(unsigned i=0;i<16;++i)text[at++]=digits[(status>>((15-i)*4))&15];
    text[at]=0;
}
}
