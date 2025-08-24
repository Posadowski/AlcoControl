#include "GpioOneWireTempBus.hpp"
#include <cstring>

extern "C" {
#include "esp_rom_sys.h"   // esp_rom_delay_us
}

using namespace alco::core;
using namespace alco::onewire;

// --- opkody DS18B20 ---
static constexpr uint8_t CMD_SEARCH_ROM      = 0xF0;
static constexpr uint8_t CMD_READ_ROM        = 0x33;
static constexpr uint8_t CMD_MATCH_ROM       = 0x55;
static constexpr uint8_t CMD_SKIP_ROM        = 0xCC;
static constexpr uint8_t CMD_CONVERT_T       = 0x44;
static constexpr uint8_t CMD_READ_SCRATCHPAD = 0xBE;
static constexpr uint8_t DS18B20_FAMILY_CODE = 0x28;

GpioOneWireTempBus::GpioOneWireTempBus(gpio_num_t pin, uint8_t resolutionBits,
                                       bool parasitePower, std::string name)
: pin_(pin), resBits_(resolutionBits), parasite_(parasitePower), name_(std::move(name)) {}

Status GpioOneWireTempBus::init() {
    if (resBits_ < 9 || resBits_ > 12) return {StatusCode::InvalidState, "resolution 9..12"};
    if (!GPIO_IS_VALID_GPIO(pin_))      return {StatusCode::InvalidState, "invalid gpio"};
    gpio_reset_pin(pin_);
    // „release” == wejście z podciągnięciem zewnętrznym (HIGH-Z)
    releaseBus();
    return Status::ok();
}

std::string GpioOneWireTempBus::name() const { return name_; }

// --- Timing helpers ---
void GpioOneWireTempBus::delayUs(uint32_t us) { esp_rom_delay_us(us); }

inline void GpioOneWireTempBus::driveLow() {
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 0);
}
inline void GpioOneWireTempBus::releaseBus() {
    // HIGH-Z: wejście, zakładamy zewnętrzny pull-up 4.7k
    gpio_set_direction(pin_, GPIO_MODE_INPUT);
}
inline int GpioOneWireTempBus::readPin() {
    return gpio_get_level(pin_);
}

// --- 1-Wire primitives ---
bool GpioOneWireTempBus::resetPresence() {
    // reset: pull low 480us
    driveLow();
    delayUs(480);
    releaseBus();
    // presence: po ~70us próbkuj
    delayUs(70);
    int presence = (readPin() == 0); // urządzenie ściąga do zera
    // czekamy do końca okna (480-70 = ~410us)
    delayUs(410);
    return presence;
}

void GpioOneWireTempBus::writeBit(int bit) {
    if (bit) {
        // write '1': low ~6us, release do końca slotu (~60us)
        driveLow(); delayUs(6);
        releaseBus(); delayUs(64);
    } else {
        // write '0': low ~60us
        driveLow(); delayUs(60);
        releaseBus(); delayUs(10);
    }
}

int GpioOneWireTempBus::readBit() {
    // read: low ~6us, release, po ~9us próbkuj
    driveLow(); delayUs(6);
    releaseBus(); delayUs(9);
    int bit = readPin();
    // dopełnij slot (~55us)
    delayUs(55);
    return bit ? 1 : 0;
}

void GpioOneWireTempBus::writeByte(uint8_t v) {
    for (int i = 0; i < 8; ++i) writeBit((v >> i) & 1);
}

uint8_t GpioOneWireTempBus::readByte() {
    uint8_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (readBit() << i);
    return v;
}

// --- ROM level ---
void GpioOneWireTempBus::matchRom(const std::string& rom) {
    writeByte(CMD_MATCH_ROM);
    for (int i = 0; i < 8; ++i) writeByte(static_cast<uint8_t>(rom[i]));
}

void GpioOneWireTempBus::skipRom() {
    writeByte(CMD_SKIP_ROM);
}

// --- CRC8 Dallas (0x31, rev poly 0x8C) ---
uint8_t GpioOneWireTempBus::crc8Dallas(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i=0; i<len; ++i) {
        uint8_t inbyte = data[i];
        for (int j=0; j<8; ++j) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}
bool GpioOneWireTempBus::crcOk(const uint8_t* data, size_t len) {
    return crc8Dallas(data, len-1) == data[len-1];
}

// --- Search ROM (Dallas algorithm) ---
bool GpioOneWireTempBus::searchNext(std::string& rom, int& lastDiscrepancy, bool& lastDeviceFlag) {
    // Implementacja klasyczna (pojedyncze urządzenie: działa od razu; wiele: iteruje)
    uint8_t id[8] = {0};
    int idBitNumber = 1;
    int lastZero = 0;
    int romByteNumber = 0;
    uint8_t romByteMask = 1;

    if (lastDeviceFlag) return false;
    if (!resetPresence()) return false;

    writeByte(CMD_SEARCH_ROM);

    do {
        int idBit   = readBit();
        int cmpBit  = readBit();

        if (idBit == 1 && cmpBit == 1) {
            // brak urządzeń
            break;
        }

        int searchDirection;
        if (idBit != cmpBit) {
            // bez konfliktu — bierz idBit
            searchDirection = idBit;
        } else {
            // konflikt — wybór zależy od lastDiscrepancy
            if (idBitNumber < lastDiscrepancy) {
                searchDirection = ((id[romByteNumber] & romByteMask) > 0);
            } else {
                searchDirection = (idBitNumber == lastDiscrepancy);
            }
            if (searchDirection == 0) lastZero = idBitNumber;
        }

        if (searchDirection) id[romByteNumber] |= romByteMask;
        else                 id[romByteNumber] &= ~romByteMask;

        writeBit(searchDirection);

        idBitNumber++;
        romByteMask <<= 1;
        if (romByteMask == 0) {
            romByteNumber++;
            romByteMask = 1;
        }
    } while (romByteNumber < 8);

    if (idBitNumber < 65) return false; // niepełny ROM

    // CRC i family code
    if (id[0] != DS18B20_FAMILY_CODE) return false;
    if (crc8Dallas(id, 7) != id[7])    return false;

    // Zwróć ROM jako 8 bajtów (LSB-first)
    rom.assign(reinterpret_cast<char*>(id), 8);

    lastDiscrepancy = lastZero;
    if (lastDiscrepancy == 0) lastDeviceFlag = true;
    return true;
}

Status GpioOneWireTempBus::search(std::vector<OneWireRom>& out) {
    out.clear();
    int lastDiscrepancy = 0;
    bool lastDeviceFlag = false;
    std::string rom;

    do {
        rom.clear();
        if (!searchNext(rom, lastDiscrepancy, lastDeviceFlag)) break;
        // zamień 8 bajtów na czytelny string "28-XXXXXXXXXXXXXX"
        char buf[3*8] = {0};
        // ROM jest LSB-first; do tekstu wypiszemy MSB-last (konwencja bywa różna)
        for (int i=7; i>=0; --i) {
            sprintf(buf + (7-i)*2, "%02X", static_cast<uint8_t>(rom[i]));
        }
        out.push_back({std::string("28-") + std::string(buf, 16)});
    } while (!lastDeviceFlag);

    if (out.empty()) return {StatusCode::NotFound, "no devices"};
    return Status::ok();
}

uint32_t GpioOneWireTempBus::conversionTimeUs() const {
    // 9b:  94ms, 10b: 188ms, 11b: 375ms, 12b: 750ms (wartości typowe)
    switch (resBits_) {
        case 9:  return 95'000;
        case 10: return 190'000;
        case 11: return 375'000;
        default: return 750'000;
    }
}

Status GpioOneWireTempBus::startConversion(const OneWireRom& dev) {
    if (!resetPresence()) return {StatusCode::Timeout, "no presence"};
    // Jeśli dev.rom == "28-......" to lepiej użyć MATCH ROM.
    // U nas dev.rom jest w postaci tekstowej — potrzebujemy binarnego 8B.
    // Minimalnie: użyj SKIP ROM (konwersja wszystkich) — prościej i OK dla wspólnego pomiaru.
    skipRom();
    writeByte(CMD_CONVERT_T);

    if (!parasite_) {
        // Zasilanie normalne — możemy po prostu poczekać tyle ile trzeba
        delayUs(conversionTimeUs());
    } else {
        // Parasite power: w realu trzeba zrobić strong pull-up na czas konwersji.
        // Tu tylko czekamy (zakładamy zewnętrzny driver/strong pull-up sprzętowy).
        delayUs(conversionTimeUs());
    }
    return Status::ok();
}

Status GpioOneWireTempBus::readTemperature(const OneWireRom& dev, float& outCelsius) {
    // przekształć tekst "28-XXXXXXXXXXXXXX" do 8B ROM jeśli chcesz MATCH ROM
    // dla uproszczenia: użyjemy MATCH ROM tylko gdy parsowanie się uda
    uint8_t romBin[8] = {0};
    bool romOk = false;
    if (dev.rom.size() >= 3+16) {
        // pomiń "28-"
        for (int i=0; i<8; ++i) {
            char hi = dev.rom[3 + (7-i)*2];
            char lo = dev.rom[3 + (7-i)*2 + 1];
            auto hex = [](char c)->int{ if(c>='0'&&c<='9')return c-'0';
                                        if(c>='A'&&c<='F')return c-'A'+10;
                                        if(c>='a'&&c<='f')return c-'a'+10;
                                        return 0; };
            romBin[i] = (hex(hi)<<4) | hex(lo);
        }
        // szybka weryfikacja
        if (romBin[0] == DS18B20_FAMILY_CODE && crc8Dallas(romBin, 7) == romBin[7]) romOk = true;
    }

    if (!resetPresence()) return {StatusCode::Timeout, "no presence"};
    if (romOk) {
        writeByte(CMD_MATCH_ROM);
        for (int i=0; i<8; ++i) writeByte(romBin[i]);
    } else {
        skipRom(); // awaryjnie czytaj pierwszy
    }
    writeByte(CMD_READ_SCRATCHPAD);

    uint8_t scratch[9] = {0};
    for (int i=0; i<9; ++i) scratch[i] = readByte();
    if (!crcOk(scratch, 9)) return {StatusCode::BusError, "crc"};

    int16_t raw = (scratch[1] << 8) | scratch[0];
    // Raw LSB = 1/16 °C przy 12b; niższe rozdzielczości: młodsze bity nieważne.
    outCelsius = static_cast<float>(raw) / 16.0f;
    return Status::ok();
}
