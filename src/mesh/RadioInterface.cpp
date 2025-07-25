#include "RadioInterface.h"
#include "Channels.h"
#include "DisplayFormatters.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include "sleep.h"
#include <assert.h>
#include <pb_decode.h>
#include <pb_encode.h>

// Calculate 2^n without calling pow()
uint32_t pow_of_2(uint32_t n)
{
    return 1 << n;
}

#define RDEF(name, freq_start, freq_end, duty_cycle, spacing, power_limit, audio_permitted, frequency_switching, wide_lora)      \
    {                                                                                                                            \
        meshtastic_Config_LoRaConfig_RegionCode_##name, freq_start, freq_end, duty_cycle, spacing, power_limit, audio_permitted, \
            frequency_switching, wide_lora, #name                                                                                \
    }

const RegionInfo regions[] = {
    /*
        https://link.springer.com/content/pdf/bbm%3A978-1-4842-4357-2%2F1.pdf
        https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/
    */
    RDEF(US, 902.0f, 928.0f, 100, 0, 30, true, false, false),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(EU_433, 433.0f, 434.0f, 10, 0, 12, true, false, false),

    /*
       https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/
       https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/
       https://www.legislation.gov.uk/uksi/1999/930/schedule/6/part/III/made/data.xht?view=snippet&wrap=true

       audio_permitted = false per regulation

       Special Note:
       The link above describes LoRaWAN's band plan, stating a power limit of 16 dBm. This is their own suggested specification,
       we do not need to follow it. The European Union regulations clearly state that the power limit for this frequency range is
       500 mW, or 27 dBm. It also states that we can use interference avoidance and spectrum access techniques (such as LBT +
       AFA) to avoid a duty cycle. (Please refer to line P page 22 of this document.)
       https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.01.01_60/en_30022002v030101p.pdf
     */
    RDEF(EU_868, 869.4f, 869.65f, 10, 0, 27, false, false, false),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(CN, 470.0f, 510.0f, 100, 0, 19, true, false, false),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
        https://www.arib.or.jp/english/html/overview/doc/5-STD-T108v1_5-E1.pdf
        https://qiita.com/ammo0613/items/d952154f1195b64dc29f
     */
    RDEF(JP, 920.5f, 923.5f, 100, 0, 13, true, false, false),

    /*
        https://www.iot.org.au/wp/wp-content/uploads/2016/12/IoTSpectrumFactSheet.pdf
        https://iotalliance.org.nz/wp-content/uploads/sites/4/2019/05/IoT-Spectrum-in-NZ-Briefing-Paper.pdf
        Also used in Brazil.
     */
    RDEF(ANZ, 915.0f, 928.0f, 100, 0, 30, true, false, false),

    /*
        433.05 - 434.79 MHz, 25mW EIRP max, No duty cycle restrictions
        AU Low Interference Potential https://www.acma.gov.au/licences/low-interference-potential-devices-lipd-class-licence
        NZ General User Radio Licence for Short Range Devices https://gazette.govt.nz/notice/id/2022-go3100
     */
    RDEF(ANZ_433, 433.05f, 434.79f, 100, 0, 14, true, false, false),

    /*
        https://digital.gov.ru/uploaded/files/prilozhenie-12-k-reshenyu-gkrch-18-46-03-1.pdf

        Note:
            - We do LBT, so 100% is allowed.
     */
    RDEF(RU, 868.7f, 869.2f, 100, 0, 20, true, false, false),

    /*
        https://www.law.go.kr/LSW/admRulLsInfoP.do?admRulId=53943&efYd=0
        https://resources.lora-alliance.org/technical-specifications/rp002-1-0-4-regional-parameters
     */
    RDEF(KR, 920.0f, 923.0f, 100, 0, 23, true, false, false),

    /*
        Taiwan, 920-925Mhz, limited to 0.5W indoor or coastal, 1.0W outdoor.
        5.8.1 in the Low-power Radio-frequency Devices Technical Regulations
        https://www.ncc.gov.tw/english/files/23070/102_5190_230703_1_doc_C.PDF
        https://gazette.nat.gov.tw/egFront/e_detail.do?metaid=147283
     */
    RDEF(TW, 920.0f, 925.0f, 100, 0, 27, true, false, false),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(IN, 865.0f, 867.0f, 100, 0, 30, true, false, false),

    /*
         https://rrf.rsm.govt.nz/smart-web/smart/page/-smart/domain/licence/LicenceSummary.wdk?id=219752
         https://iotalliance.org.nz/wp-content/uploads/sites/4/2019/05/IoT-Spectrum-in-NZ-Briefing-Paper.pdf
      */
    RDEF(NZ_865, 864.0f, 868.0f, 100, 0, 36, true, false, false),

    /*
       https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
    */
    RDEF(TH, 920.0f, 925.0f, 100, 0, 16, true, false, false),

    /*
        433,05-434,7 Mhz 10 mW
        https://nkrzi.gov.ua/images/upload/256/5810/PDF_UUZ_19_01_2016.pdf
    */
    RDEF(UA_433, 433.0f, 434.7f, 10, 0, 10, true, false, false),

    /*
        868,0-868,6 Mhz 25 mW
        https://nkrzi.gov.ua/images/upload/256/5810/PDF_UUZ_19_01_2016.pdf
    */
    RDEF(UA_868, 868.0f, 868.6f, 1, 0, 14, true, false, false),

    /*
        Malaysia
        433 - 435 MHz at 100mW, no restrictions.
        https://www.mcmc.gov.my/skmmgovmy/media/General/pdf/Short-Range-Devices-Specification.pdf
    */
    RDEF(MY_433, 433.0f, 435.0f, 100, 0, 20, true, false, false),

    /*
        Malaysia
        919 - 923 Mhz at 500mW, no restrictions.
        923 - 924 MHz at 500mW with 1% duty cycle OR frequency hopping.
        Frequency hopping is used for 919 - 923 MHz.
        https://www.mcmc.gov.my/skmmgovmy/media/General/pdf/Short-Range-Devices-Specification.pdf
    */
    RDEF(MY_919, 919.0f, 924.0f, 100, 0, 27, true, true, false),

    /*
        Singapore
        SG_923 Band 30d: 917 - 925 MHz at 100mW, no restrictions.
        https://www.imda.gov.sg/-/media/imda/files/regulation-licensing-and-consultations/ict-standards/telecommunication-standards/radio-comms/imdatssrd.pdf
    */
    RDEF(SG_923, 917.0f, 925.0f, 100, 0, 20, true, false, false),

    /*
        Philippines
                433 - 434.7 MHz <10 mW erp, NTC approved device required
                868 - 869.4 MHz <25 mW erp, NTC approved device required
                915 - 918 MHz <250 mW EIRP, no external antenna allowed
                https://github.com/meshtastic/firmware/issues/4948#issuecomment-2394926135
    */

    RDEF(PH_433, 433.0f, 434.7f, 100, 0, 10, true, false, false), RDEF(PH_868, 868.0f, 869.4f, 100, 0, 14, true, false, false),
    RDEF(PH_915, 915.0f, 918.0f, 100, 0, 24, true, false, false),

    /*
        Kazakhstan
                433.075 - 434.775 MHz <10 mW EIRP, Low Powered Devices (LPD)
                863 - 868 MHz <25 mW EIRP, 500kHz channels allowed, must not be used at airfields
                                https://github.com/meshtastic/firmware/issues/7204
    */
    RDEF(KZ_433, 433.075f, 434.775f, 100, 0, 10, true, false, false), RDEF(KZ_863, 863.0f, 868.0f, 100, 0, 30, true, false, true),


    /*
        Nepal
        865 MHz to 868 MHz frequency band for IoT (Internet of Things), M2M (Machine-to-Machine), and smart metering use, specifically in non-cellular mode.
        https://www.nta.gov.np/uploads/contents/Radio-Frequency-Policy-2080-English.pdf
    */
    RDEF(NP_865, 865.0f, 868.0f, 100, 0, 30, true, false, false),

    /*
        Brazil
        902 - 907.5 MHz , 1W power limit, no duty cycle restrictions
        https://github.com/meshtastic/firmware/issues/3741
    */
    RDEF(BR_902, 902.0f, 907.5f, 100, 0, 30, true, false, false),

    /*
       2.4 GHZ WLAN Band equivalent. Only for SX128x chips.
    */
    RDEF(LORA_24, 2400.0f, 2483.5f, 100, 0, 10, true, false, true),

    /*
        This needs to be last. Same as US.
    */
    RDEF(UNSET, 902.0f, 928.0f, 100, 0, 30, true, false, false)

};

const RegionInfo *myRegion;
bool RadioInterface::uses_default_frequency_slot = true;

static uint8_t bytes[MAX_LORA_PAYLOAD_LEN + 1];

void initRegion()
{
    const RegionInfo *r = regions;
#ifdef REGULATORY_LORA_REGIONCODE
    for (; r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET && r->code != REGULATORY_LORA_REGIONCODE; r++)
        ;
    LOG_INFO("Wanted region %d, regulatory override to %s", config.lora.region, r->name);
#else
    for (; r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET && r->code != config.lora.region; r++)
        ;
    LOG_INFO("Wanted region %d, using %s", config.lora.region, r->name);
#endif
    myRegion = r;
}

/**
 * ## LoRaWAN for North America

LoRaWAN defines 64, 125 kHz channels from 902.3 to 914.9 MHz increments.

The maximum output power for North America is +30 dBM.

The band is from 902 to 928 MHz. It mentions channel number and its respective channel frequency. All the 13 channels are
separated by 2.16 MHz with respect to the adjacent channels. Channel zero starts at 903.08 MHz center frequency.
*/

/**
 * Calculate airtime per
 * https://www.rs-online.com/designspark/rel-assets/ds-assets/uploads/knowledge-items/application-notes-for-the-internet-of-things/LoRa%20Design%20Guide.pdf
 * section 4
 *
 * @return num msecs for the packet
 */
uint32_t RadioInterface::getPacketTime(uint32_t pl)
{
    float bandwidthHz = bw * 1000.0f;
    bool headDisable = false; // we currently always use the header
    float tSym = (1 << sf) / bandwidthHz;

    bool lowDataOptEn = tSym > 16e-3 ? true : false; // Needed if symbol time is >16ms

    float tPreamble = (preambleLength + 4.25f) * tSym;
    float numPayloadSym =
        8 + max(ceilf(((8.0f * pl - 4 * sf + 28 + 16 - 20 * headDisable) / (4 * (sf - 2 * lowDataOptEn))) * cr), 0.0f);
    float tPayload = numPayloadSym * tSym;
    float tPacket = tPreamble + tPayload;

    uint32_t msecs = tPacket * 1000;

    return msecs;
}

uint32_t RadioInterface::getPacketTime(const meshtastic_MeshPacket *p)
{
    uint32_t pl = 0;
    if (p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        pl = p->encrypted.size + sizeof(PacketHeader);
    } else {
        size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_Data_msg, &p->decoded);
        pl = numbytes + sizeof(PacketHeader);
    }
    return getPacketTime(pl);
}

/** The delay to use for retransmitting dropped packets */
uint32_t RadioInterface::getRetransmissionMsec(const meshtastic_MeshPacket *p)
{
    size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_Data_msg, &p->decoded);
    uint32_t packetAirtime = getPacketTime(numbytes + sizeof(PacketHeader));
    // Make sure enough time has elapsed for this packet to be sent and an ACK is received.
    // LOG_DEBUG("Waiting for flooding message with airtime %d and slotTime is %d", packetAirtime, slotTimeMsec);
    float channelUtil = airTime->channelUtilizationPercent();
    uint8_t CWsize = map(channelUtil, 0, 100, CWmin, CWmax);
    // Assuming we pick max. of CWsize and there will be a client with SNR at half the range
    return 2 * packetAirtime + (pow_of_2(CWsize) + 2 * CWmax + pow_of_2(int((CWmax + CWmin) / 2))) * slotTimeMsec +
           PROCESSING_TIME_MSEC;
}

/** The delay to use when we want to send something */
uint32_t RadioInterface::getTxDelayMsec()
{
    /** We wait a random multiple of 'slotTimes' (see definition in header file) in order to avoid collisions.
    The pool to take a random multiple from is the contention window (CW), which size depends on the
    current channel utilization. */
    float channelUtil = airTime->channelUtilizationPercent();
    uint8_t CWsize = map(channelUtil, 0, 100, CWmin, CWmax);
    // LOG_DEBUG("Current channel utilization is %f so setting CWsize to %d", channelUtil, CWsize);
    return random(0, pow_of_2(CWsize)) * slotTimeMsec;
}

/** The CW size to use when calculating SNR_based delays */
uint8_t RadioInterface::getCWsize(float snr)
{
    // The minimum value for a LoRa SNR
    const uint32_t SNR_MIN = -20;

    // The maximum value for a LoRa SNR
    const uint32_t SNR_MAX = 10;

    return map(snr, SNR_MIN, SNR_MAX, CWmin, CWmax);
}

/** The worst-case SNR_based packet delay */
uint32_t RadioInterface::getTxDelayMsecWeightedWorst(float snr)
{
    uint8_t CWsize = getCWsize(snr);
    // offset the maximum delay for routers: (2 * CWmax * slotTimeMsec)
    return (2 * CWmax * slotTimeMsec) + pow_of_2(CWsize) * slotTimeMsec;
}

/** The delay to use when we want to flood a message */
uint32_t RadioInterface::getTxDelayMsecWeighted(float snr)
{
    //  high SNR = large CW size (Long Delay)
    //  low SNR = small CW size (Short Delay)
    uint32_t delay = 0;
    uint8_t CWsize = getCWsize(snr);
    // LOG_DEBUG("rx_snr of %f so setting CWsize to:%d", snr, CWsize);
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
        config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        delay = random(0, 2 * CWsize) * slotTimeMsec;
        LOG_DEBUG("rx_snr found in packet. Router: setting tx delay:%d", delay);
    } else {
        // offset the maximum delay for routers: (2 * CWmax * slotTimeMsec)
        delay = (2 * CWmax * slotTimeMsec) + random(0, pow_of_2(CWsize)) * slotTimeMsec;
        LOG_DEBUG("rx_snr found in packet. Setting tx delay:%d", delay);
    }

    return delay;
}

void printPacket(const char *prefix, const meshtastic_MeshPacket *p)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    std::string out = DEBUG_PORT.mt_sprintf("%s (id=0x%08x fr=0x%08x to=0x%08x, WantAck=%d, HopLim=%d Ch=0x%x", prefix, p->id,
                                            p->from, p->to, p->want_ack, p->hop_limit, p->channel);
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        auto &s = p->decoded;

        out += DEBUG_PORT.mt_sprintf(" Portnum=%d", s.portnum);

        if (s.want_response)
            out += DEBUG_PORT.mt_sprintf(" WANTRESP");

        if (p->pki_encrypted)
            out += DEBUG_PORT.mt_sprintf(" PKI");

        if (s.source != 0)
            out += DEBUG_PORT.mt_sprintf(" source=%08x", s.source);

        if (s.dest != 0)
            out += DEBUG_PORT.mt_sprintf(" dest=%08x", s.dest);

        if (s.request_id)
            out += DEBUG_PORT.mt_sprintf(" requestId=%0x", s.request_id);

        /* now inside Data and therefore kinda opaque
        if (s.which_ackVariant == SubPacket_success_id_tag)
            out += DEBUG_PORT.mt_sprintf(" successId=%08x", s.ackVariant.success_id);
        else if (s.which_ackVariant == SubPacket_fail_id_tag)
            out += DEBUG_PORT.mt_sprintf(" failId=%08x", s.ackVariant.fail_id); */
    } else {
        out += " encrypted";
        out += DEBUG_PORT.mt_sprintf(" len=%d", p->encrypted.size + sizeof(PacketHeader));
    }

    if (p->rx_time != 0)
        out += DEBUG_PORT.mt_sprintf(" rxtime=%u", p->rx_time);
    if (p->rx_snr != 0.0)
        out += DEBUG_PORT.mt_sprintf(" rxSNR=%g", p->rx_snr);
    if (p->rx_rssi != 0)
        out += DEBUG_PORT.mt_sprintf(" rxRSSI=%i", p->rx_rssi);
    if (p->via_mqtt != 0)
        out += DEBUG_PORT.mt_sprintf(" via MQTT");
    if (p->hop_start != 0)
        out += DEBUG_PORT.mt_sprintf(" hopStart=%d", p->hop_start);
    if (p->next_hop != 0)
        out += DEBUG_PORT.mt_sprintf(" nextHop=0x%x", p->next_hop);
    if (p->relay_node != 0)
        out += DEBUG_PORT.mt_sprintf(" relay=0x%x", p->relay_node);
    if (p->priority != 0)
        out += DEBUG_PORT.mt_sprintf(" priority=%d", p->priority);

    out += ")";
    LOG_DEBUG("%s", out.c_str());
#endif
}

RadioInterface::RadioInterface()
{
    assert(sizeof(PacketHeader) == MESHTASTIC_HEADER_LENGTH); // make sure the compiler did what we expected
}

bool RadioInterface::reconfigure()
{
    applyModemConfig();
    return true;
}

bool RadioInterface::init()
{
    LOG_INFO("Start meshradio init");

    configChangedObserver.observe(&service->configChanged);
    preflightSleepObserver.observe(&preflightSleep);
    notifyDeepSleepObserver.observe(&notifyDeepSleep);

    // we now expect interfaces to operate in promiscuous mode
    // radioIf.setThisAddress(nodeDB->getNodeNum()); // Note: we must do this here, because the nodenum isn't inited at
    // constructor time.

    applyModemConfig();

    return true;
}

int RadioInterface::notifyDeepSleepCb(void *unused)
{
    sleep();
    return 0;
}

/** hash a string into an integer
 *
 * djb2 by Dan Bernstein.
 * http://www.cse.yorku.ca/~oz/hash.html
 */
uint32_t hash(const char *str)
{
    uint32_t hash = 5381;
    int c;

    while ((c = *str++) != 0)
        hash = ((hash << 5) + hash) + (unsigned char)c; /* hash * 33 + c */

    return hash;
}

/**
 * Save our frequency for later reuse.
 */
void RadioInterface::saveFreq(float freq)
{
    savedFreq = freq;
}

/**
 * Save our channel for later reuse.
 */
void RadioInterface::saveChannelNum(uint32_t channel_num)
{
    savedChannelNum = channel_num;
}

/**
 * Save our frequency for later reuse.
 */
float RadioInterface::getFreq()
{
    return savedFreq;
}

/**
 * Save our channel for later reuse.
 */
uint32_t RadioInterface::getChannelNum()
{
    return savedChannelNum;
}

/**
 * Pull our channel settings etc... from protobufs to the dumb interface settings
 */
void RadioInterface::applyModemConfig()
{
    // Set up default configuration
    // No Sync Words in LORA mode
    meshtastic_Config_LoRaConfig &loraConfig = config.lora;
    bool validConfig = false; // We need to check for a valid configuration
    while (!validConfig) {
        if (loraConfig.use_preset) {

            switch (loraConfig.modem_preset) {
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
                bw = (myRegion->wideLora) ? 1625.0 : 500;
                cr = 5;
                sf = 7;
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
                bw = (myRegion->wideLora) ? 812.5 : 250;
                cr = 5;
                sf = 7;
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
                bw = (myRegion->wideLora) ? 812.5 : 250;
                cr = 5;
                sf = 8;
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
                bw = (myRegion->wideLora) ? 812.5 : 250;
                cr = 5;
                sf = 9;
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
                bw = (myRegion->wideLora) ? 812.5 : 250;
                cr = 5;
                sf = 10;
                break;
            default: // Config_LoRaConfig_ModemPreset_LONG_FAST is default. Gracefully use this is preset is something illegal.
                bw = (myRegion->wideLora) ? 812.5 : 250;
                cr = 5;
                sf = 11;
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
                bw = (myRegion->wideLora) ? 406.25 : 125;
                cr = 8;
                sf = 11;
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
                bw = (myRegion->wideLora) ? 406.25 : 125;
                cr = 8;
                sf = 12;
                break;
            }
        } else {
            sf = loraConfig.spread_factor;
            cr = loraConfig.coding_rate;
            bw = loraConfig.bandwidth;

            if (bw == 31) // This parameter is not an integer
                bw = 31.25;
            if (bw == 62) // Fix for 62.5Khz bandwidth
                bw = 62.5;
            if (bw == 200)
                bw = 203.125;
            if (bw == 400)
                bw = 406.25;
            if (bw == 800)
                bw = 812.5;
            if (bw == 1600)
                bw = 1625.0;
        }

        if ((myRegion->freqEnd - myRegion->freqStart) < bw / 1000) {
            static const char *err_string = "Regional frequency range is smaller than bandwidth. Fall back to default preset";
            LOG_ERROR(err_string);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

            meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
            cn->level = meshtastic_LogRecord_Level_ERROR;
            sprintf(cn->message, err_string);
            service->sendClientNotification(cn);

            // Set to default modem preset
            loraConfig.use_preset = true;
            loraConfig.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
        } else {
            validConfig = true;
        }
    }

    power = loraConfig.tx_power;

    if ((power == 0) || ((power > myRegion->powerLimit) && !devicestate.owner.is_licensed))
        power = myRegion->powerLimit;

    if (power == 0)
        power = 17; // Default to this power level if we don't have a valid regional power limit (powerLimit of myRegion defaults
                    // to 0, currently no region has an actual power limit of 0 [dBm] so we can assume regions which have this
                    // variable set to 0 don't have a valid power limit)

    // Set final tx_power back onto config
    loraConfig.tx_power = (int8_t)power; // cppcheck-suppress assignmentAddressToInteger

    // Calculate the number of channels
    uint32_t numChannels = floor((myRegion->freqEnd - myRegion->freqStart) / (myRegion->spacing + (bw / 1000)));

    // If user has manually specified a channel num, then use that, otherwise generate one by hashing the name
    const char *channelName = channels.getName(channels.getPrimaryIndex());
    // channel_num is actually (channel_num - 1), since modulus (%) returns values from 0 to (numChannels - 1)
    uint32_t channel_num = (loraConfig.channel_num ? loraConfig.channel_num - 1 : hash(channelName)) % numChannels;

    // Check if we use the default frequency slot
    RadioInterface::uses_default_frequency_slot =
        channel_num == hash(DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false)) % numChannels;

    // Old frequency selection formula
    // float freq = myRegion->freqStart + ((((myRegion->freqEnd - myRegion->freqStart) / numChannels) / 2) * channel_num);

    // New frequency selection formula
    float freq = myRegion->freqStart + (bw / 2000) + (channel_num * (bw / 1000));

    // override if we have a verbatim frequency
    if (loraConfig.override_frequency) {
        freq = loraConfig.override_frequency;
        channel_num = -1;
    }

    saveChannelNum(channel_num);
    saveFreq(freq + loraConfig.frequency_offset);

    slotTimeMsec = computeSlotTimeMsec();
    preambleTimeMsec = getPacketTime((uint32_t)0);
    maxPacketTimeMsec = getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader));

    LOG_INFO("Radio freq=%.3f, config.lora.frequency_offset=%.3f", freq, loraConfig.frequency_offset);
    LOG_INFO("Set radio: region=%s, name=%s, config=%u, ch=%d, power=%d", myRegion->name, channelName, loraConfig.modem_preset,
             channel_num, power);
    LOG_INFO("myRegion->freqStart -> myRegion->freqEnd: %f -> %f (%f MHz)", myRegion->freqStart, myRegion->freqEnd,
             myRegion->freqEnd - myRegion->freqStart);
    LOG_INFO("numChannels: %d x %.3fkHz", numChannels, bw);
    LOG_INFO("channel_num: %d", channel_num + 1);
    LOG_INFO("frequency: %f", getFreq());
    LOG_INFO("Slot time: %u msec", slotTimeMsec);
}

/** Slottime is the time to detect a transmission has started, consisting of:
  - CAD duration;
  - roundtrip air propagation time (assuming max. 30km between nodes);
  - Tx/Rx turnaround time (maximum of SX126x and SX127x);
  - MAC processing time (measured on T-beam) */
uint32_t RadioInterface::computeSlotTimeMsec()
{
    float sumPropagationTurnaroundMACTime = 0.2 + 0.4 + 7; // in milliseconds
    float symbolTime = pow_of_2(sf) / bw;                  // in milliseconds

    if (myRegion->wideLora) {
        // CAD duration derived from AN1200.22 of SX1280
        return (NUM_SYM_CAD_24GHZ + (2 * sf + 3) / 32) * symbolTime + sumPropagationTurnaroundMACTime;
    } else {
        // CAD duration for SX127x is max. 2.25 symbols, for SX126x it is number of symbols + 0.5 symbol
        return max(2.25, NUM_SYM_CAD + 0.5) * symbolTime + sumPropagationTurnaroundMACTime;
    }
}

/**
 * Some regulatory regions limit xmit power.
 * This function should be called by subclasses after setting their desired power.  It might lower it
 */
void RadioInterface::limitPower(int8_t loraMaxPower)
{
    uint8_t maxPower = 255; // No limit

    if (myRegion->powerLimit)
        maxPower = myRegion->powerLimit;

    if ((power > maxPower) && !devicestate.owner.is_licensed) {
        LOG_INFO("Lower transmit power because of regulatory limits");
        power = maxPower;
    }

    if (TX_GAIN_LORA > 0) {
        LOG_INFO("Requested Tx power: %d dBm; Device LoRa Tx gain: %d dB", power, TX_GAIN_LORA);
        power -= TX_GAIN_LORA;
    }

    if (power > loraMaxPower) // Clamp power to maximum defined level
        power = loraMaxPower;

    LOG_INFO("Final Tx power: %d dBm", power);
}

void RadioInterface::deliverToReceiver(meshtastic_MeshPacket *p)
{
    if (router)
        router->enqueueReceivedMessage(p);
}

/***
 * given a packet set sendingPacket and decode the protobufs into radiobuf.  Returns # of payload bytes to send
 */
size_t RadioInterface::beginSending(meshtastic_MeshPacket *p)
{
    assert(!sendingPacket);

    // LOG_DEBUG("Send queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
    assert(p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag); // It should have already been encoded by now

    radioBuffer.header.from = p->from;
    radioBuffer.header.to = p->to;
    radioBuffer.header.id = p->id;
    radioBuffer.header.channel = p->channel;
    radioBuffer.header.next_hop = p->next_hop;
    radioBuffer.header.relay_node = p->relay_node;
    if (p->hop_limit > HOP_MAX) {
        LOG_WARN("hop limit %d is too high, setting to %d", p->hop_limit, HOP_RELIABLE);
        p->hop_limit = HOP_RELIABLE;
    }
    radioBuffer.header.flags =
        p->hop_limit | (p->want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0) | (p->via_mqtt ? PACKET_FLAGS_VIA_MQTT_MASK : 0);
    radioBuffer.header.flags |= (p->hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK;

    // if the sender nodenum is zero, that means uninitialized
    assert(radioBuffer.header.from);
    assert(p->encrypted.size <= sizeof(radioBuffer.payload));
    memcpy(radioBuffer.payload, p->encrypted.bytes, p->encrypted.size);

    sendingPacket = p;
    return p->encrypted.size + sizeof(PacketHeader);
}
