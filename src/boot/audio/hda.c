/*
 * Intel High Definition Audio output driver.
 *
 * Runs polled, with interrupts off. Register access and DMA buffers go through
 * EFI_PCI_IO_PROTOCOL. The codec graph is walked at runtime: from every
 * connected speaker/headphone/line-out pin back to one DAC.
 */
#include "audio/audio.h"
#include "lib/lib.h"

/* Controller registers */
#define GCAP      0x00
#define GCTL      0x08
#define STATESTS  0x0E
#define INTCTL    0x20
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP    0x48
#define CORBRP    0x4A
#define CORBCTL   0x4C
#define CORBSIZE  0x4E
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP    0x58
#define RINTCNT   0x5A
#define RIRBCTL   0x5C
#define RIRBSTS   0x5D
#define RIRBSIZE  0x5E

#define GCTL_CRST     1u
#define CORBRP_RESET  0x8000u
#define RIRBWP_RESET  0x8000u
#define RING_RUN      0x02u /* CORBCTL and RIRBCTL */
#define RIRBSTS_CLEAR 0x05u

/* Stream descriptor registers, relative to the descriptor */
#define SD_CTL0 0x00
#define SD_CTL2 0x02
#define SD_LPIB 0x04
#define SD_CBL  0x08
#define SD_LVI  0x0C
#define SD_FMT  0x12
#define SD_BDPL 0x18
#define SD_BDPU 0x1C

#define SD_CTL_SRST 0x01u
#define SD_CTL_RUN  0x02u

/* Codec verbs */
#define V_GET_PARAM    0xF00
#define V_GET_CONN     0xF02
#define V_GET_CFG      0xF1C
#define V_SET_CONN     0x701
#define V_SET_POWER    0x705
#define V_SET_STREAM   0x706
#define V_SET_PIN      0x707
#define V_SET_EAPD     0x70C
#define V_SET_FORMAT_4 0x2 /* 4-bit verbs carry a 16-bit payload */
#define V_SET_AMP_4    0x3

#define P_NODE_COUNT 0x04
#define P_FUNC_TYPE  0x05
#define P_WIDGET_CAP 0x09
#define P_PIN_CAP    0x0C
#define P_AMP_IN     0x0D
#define P_CONN_LEN   0x0E
#define P_AMP_OUT    0x12

#define FUNC_AUDIO 0x01

#define WIDGET_DAC   0
#define WIDGET_MIXER 2
#define WIDGET_SEL   3
#define WIDGET_PIN   4

#define WCAP_AMP_IN   (1u << 1)
#define WCAP_AMP_OUT  (1u << 2)
#define WCAP_AMP_OVRD (1u << 3)
#define WCAP_CONN     (1u << 8)
#define WCAP_POWER    (1u << 10)
#define WIDGET_TYPE(wcap) (((wcap) >> 20) & 0xF)

#define PINCAP_HP   (1u << 3)
#define PINCAP_OUT  (1u << 4)
#define PINCAP_EAPD (1u << 16)

#define MAX_PATH  8
#define MAX_CONNS 32
#define STREAM_TAG 1
#define BDL_ENTRIES 4
#define TIMEOUT_POLLS 10000 /* 10us apart: 100ms */

#define DMA_CORB_OFFSET 0x000
#define DMA_RIRB_OFFSET 0x400
#define DMA_BDL_OFFSET  0xC00

struct dma_buffer {
    void *host;
    void *mapping;
    EFI_PHYSICAL_ADDRESS device;
    UINTN pages;
};

struct bdl_entry {
    UINT64 address;
    UINT32 length;
    UINT32 flags;
};

struct path_hop {
    UINT8 nid;
    UINT8 conn_index; /* which input leads to the next hop */
};

static struct {
    EFI_PCI_IO_PROTOCOL *pci;
    struct dma_buffer commands, ring;
    volatile UINT32 *corb, *rirb;
    UINT32 corb_mask, rirb_mask, rirb_read;
    UINT32 stream;
    UINT8 afg;
    UINT32 afg_amp_in, afg_amp_out;
} hda;

static UINT8 rd8(UINT32 reg)
{
    UINT8 v = 0;
    hda.pci->Mem.Read(hda.pci, EfiPciIoWidthUint8, 0, reg, 1, &v);
    return v;
}

static UINT16 rd16(UINT32 reg)
{
    UINT16 v = 0;
    hda.pci->Mem.Read(hda.pci, EfiPciIoWidthUint16, 0, reg, 1, &v);
    return v;
}

static UINT32 rd32(UINT32 reg)
{
    UINT32 v = 0;
    hda.pci->Mem.Read(hda.pci, EfiPciIoWidthUint32, 0, reg, 1, &v);
    return v;
}

static void wr8(UINT32 reg, UINT8 v)
{
    hda.pci->Mem.Write(hda.pci, EfiPciIoWidthUint8, 0, reg, 1, &v);
}

static void wr16(UINT32 reg, UINT16 v)
{
    hda.pci->Mem.Write(hda.pci, EfiPciIoWidthUint16, 0, reg, 1, &v);
}

static void wr32(UINT32 reg, UINT32 v)
{
    hda.pci->Mem.Write(hda.pci, EfiPciIoWidthUint32, 0, reg, 1, &v);
}

static BOOLEAN wait8(UINT32 reg, UINT8 mask, UINT8 want)
{
    for (int i = 0; i < TIMEOUT_POLLS; i++) {
        if ((rd8(reg) & mask) == want)
            return 1;
        gBS->Stall(10);
    }
    return 0;
}

static BOOLEAN wait16(UINT32 reg, UINT16 mask, UINT16 want)
{
    for (int i = 0; i < TIMEOUT_POLLS; i++) {
        if ((rd16(reg) & mask) == want)
            return 1;
        gBS->Stall(10);
    }
    return 0;
}

static EFI_STATUS dma_alloc(struct dma_buffer *b, UINTN pages)
{
    UINTN bytes = pages * 4096;
    EFI_STATUS status;

    b->pages = 0;
    status = hda.pci->AllocateBuffer(hda.pci, AllocateAnyPages, EfiBootServicesData, pages, &b->host, 0);
    if (EFI_ERROR(status))
        return status;
    status = hda.pci->Map(hda.pci, EfiPciIoBusMasterCommonBuffer, b->host, &bytes, &b->device, &b->mapping);
    if (!EFI_ERROR(status) && bytes != pages * 4096) {
        hda.pci->Unmap(hda.pci, b->mapping);
        status = EFI_OUT_OF_RESOURCES;
    }
    if (EFI_ERROR(status)) {
        hda.pci->FreeBuffer(hda.pci, pages, b->host);
        return status;
    }
    b->pages = pages;
    memset(b->host, 0, bytes);
    return EFI_SUCCESS;
}

static void dma_free(struct dma_buffer *b)
{
    if (!b->pages)
        return;
    hda.pci->Unmap(hda.pci, b->mapping);
    hda.pci->FreeBuffer(hda.pci, b->pages, b->host);
    b->pages = 0;
}

/* ---- Command transport ------------------------------------------------ */

/* Returns the register code and sets *entries for the largest ring offered. */
static UINT8 pick_ring_size(UINT8 capability, UINT32 *entries)
{
    if (capability & 0x40) {
        *entries = 256;
        return 2;
    }
    if (capability & 0x20) {
        *entries = 16;
        return 1;
    }
    *entries = 2;
    return 0;
}

static void stop_command_rings(void)
{
    wr8(CORBCTL, 0);
    wr8(RIRBCTL, 0);
    wait8(CORBCTL, RING_RUN, 0);
    wait8(RIRBCTL, RING_RUN, 0);
}

static EFI_STATUS reset_controller(void)
{
    stop_command_rings();
    wr32(GCTL, rd32(GCTL) & ~GCTL_CRST);
    if (!wait8(GCTL, GCTL_CRST, 0))
        return EFI_DEVICE_ERROR;
    gBS->Stall(100);
    wr32(GCTL, rd32(GCTL) | GCTL_CRST);
    if (!wait8(GCTL, GCTL_CRST, GCTL_CRST))
        return EFI_DEVICE_ERROR;
    gBS->Stall(2000); /* codecs need 521us to announce themselves */
    wr32(INTCTL, 0);
    return EFI_SUCCESS;
}

static void start_command_rings(void)
{
    UINT32 entries;
    UINT64 corb = hda.commands.device + DMA_CORB_OFFSET;
    UINT64 rirb = hda.commands.device + DMA_RIRB_OFFSET;

    hda.corb = (volatile UINT32 *)((UINT8 *)hda.commands.host + DMA_CORB_OFFSET);
    hda.rirb = (volatile UINT32 *)((UINT8 *)hda.commands.host + DMA_RIRB_OFFSET);

    wr32(CORBLBASE, (UINT32)corb);
    wr32(CORBUBASE, (UINT32)(corb >> 32));
    wr8(CORBSIZE, (rd8(CORBSIZE) & 0xFC) | pick_ring_size(rd8(CORBSIZE), &entries));
    hda.corb_mask = entries - 1;

    /* The read-back handshake is not honoured by every controller. */
    wr16(CORBRP, CORBRP_RESET);
    wait16(CORBRP, CORBRP_RESET, CORBRP_RESET);
    wr16(CORBRP, 0);
    wait16(CORBRP, CORBRP_RESET, 0);
    wr16(CORBWP, 0);

    wr32(RIRBLBASE, (UINT32)rirb);
    wr32(RIRBUBASE, (UINT32)(rirb >> 32));
    wr8(RIRBSIZE, (rd8(RIRBSIZE) & 0xFC) | pick_ring_size(rd8(RIRBSIZE), &entries));
    hda.rirb_mask = entries - 1;
    hda.rirb_read = 0;
    wr16(RIRBWP, RIRBWP_RESET);
    wr16(RINTCNT, 0xFF);
    wr8(RIRBSTS, RIRBSTS_CLEAR);

    wr8(RIRBCTL, RING_RUN);
    wr8(CORBCTL, RING_RUN);
    wait8(RIRBCTL, RING_RUN, RING_RUN);
    wait8(CORBCTL, RING_RUN, RING_RUN);
}

static EFI_STATUS send_raw(UINT32 codec, UINT32 nid, UINT32 payload, UINT32 *response)
{
    UINT32 next = (rd16(CORBWP) + 1) & hda.corb_mask;

    hda.corb[next] = (codec << 28) | (nid << 20) | payload;
    __asm__ volatile("" ::: "memory");
    wr16(CORBWP, (UINT16)next);

    for (int i = 0; i < TIMEOUT_POLLS; i++) {
        if ((rd16(RIRBWP) & hda.rirb_mask) != hda.rirb_read) {
            hda.rirb_read = (hda.rirb_read + 1) & hda.rirb_mask;
            *response = hda.rirb[hda.rirb_read * 2];
            wr8(RIRBSTS, RIRBSTS_CLEAR);
            return EFI_SUCCESS;
        }
        gBS->Stall(10);
    }
    return EFI_TIMEOUT;
}

/* 12-bit verb + 8-bit payload. A timed-out codec reads as all zeroes. */
static UINT32 verb(UINT32 codec, UINT32 nid, UINT32 id, UINT32 payload)
{
    UINT32 response = 0;

    if (EFI_ERROR(send_raw(codec, nid, (id << 8) | (payload & 0xFF), &response)))
        return 0;
    return response;
}

static void verb4(UINT32 codec, UINT32 nid, UINT32 id, UINT32 payload)
{
    UINT32 response;

    send_raw(codec, nid, (id << 16) | (payload & 0xFFFF), &response);
}

static UINT32 param(UINT32 codec, UINT32 nid, UINT32 id)
{
    return verb(codec, nid, V_GET_PARAM, id);
}

/* ---- Codec graph ------------------------------------------------------ */

static UINTN connections(UINT32 codec, UINT32 nid, UINT8 *out)
{
    UINT32 info = param(codec, nid, P_CONN_LEN);
    UINTN length = info & 0x7F, count = 0;
    BOOLEAN long_form = (info & 0x80) != 0;
    UINTN per_word = long_form ? 2 : 4;
    UINT32 entry_bits = long_form ? 16 : 8;
    UINT32 range_flag = 1u << (entry_bits - 1);
    UINT32 previous = 0;

    for (UINTN i = 0; i < length && count < MAX_CONNS; i++) {
        UINT32 word = verb(codec, nid, V_GET_CONN, (UINT32)(i - i % per_word));
        UINT32 entry = (word >> (entry_bits * (i % per_word))) & ((1u << entry_bits) - 1);
        UINT32 id = entry & (range_flag - 1);

        if ((entry & range_flag) && previous) {
            /* The entry is the end of a range opened by the previous one. */
            for (UINT32 n = previous + 1; n <= id && count < MAX_CONNS; n++)
                out[count++] = (UINT8)n;
        } else {
            out[count++] = (UINT8)id;
        }
        previous = id;
    }
    return count;
}

/*
 * Depth-first search towards a DAC. *dac == 0 accepts any; otherwise only that
 * one, so every pin ends up fed by the same stream.
 */
static BOOLEAN trace_to_dac(UINT32 codec, UINT32 nid, UINTN depth, struct path_hop *path,
                            UINTN *length, UINT32 *dac)
{
    UINT32 wcap = param(codec, nid, P_WIDGET_CAP);
    UINT32 type = WIDGET_TYPE(wcap);
    UINT8 conns[MAX_CONNS];
    UINTN count;

    if (depth >= MAX_PATH)
        return 0;

    path[depth].nid = (UINT8)nid;
    path[depth].conn_index = 0;

    if (type == WIDGET_DAC) {
        if (*dac && *dac != nid)
            return 0;
        *dac = nid;
        *length = depth + 1;
        return 1;
    }
    if (type != WIDGET_MIXER && type != WIDGET_SEL && !(type == WIDGET_PIN && depth == 0))
        return 0;
    if (!(wcap & WCAP_CONN))
        return 0;

    count = connections(codec, nid, conns);
    for (UINTN i = 0; i < count; i++) {
        if (trace_to_dac(codec, conns[i], depth + 1, path, length, dac)) {
            path[depth].conn_index = (UINT8)i;
            return 1;
        }
    }
    return 0;
}

/* Unmutes one amp at its 0 dB step. */
static void set_amp(UINT32 codec, UINT32 nid, UINT32 wcap, BOOLEAN output, UINT32 index)
{
    UINT32 caps;

    if (wcap & WCAP_AMP_OVRD)
        caps = param(codec, nid, output ? P_AMP_OUT : P_AMP_IN);
    else
        caps = output ? hda.afg_amp_out : hda.afg_amp_in;

    verb4(codec, nid, V_SET_AMP_4,
          (output ? 0x8000u : 0x4000u) | 0x3000u | (index << 8) | (caps & 0x7F));
}

static void enable_path(UINT32 codec, const struct path_hop *path, UINTN length)
{
    for (UINTN i = 0; i < length; i++) {
        UINT32 nid = path[i].nid;
        UINT32 wcap = param(codec, nid, P_WIDGET_CAP);
        UINT32 type = WIDGET_TYPE(wcap);
        BOOLEAN last = i + 1 == length;

        if (wcap & WCAP_POWER)
            verb(codec, nid, V_SET_POWER, 0);
        if (!last && type != WIDGET_MIXER && (wcap & WCAP_CONN))
            verb(codec, nid, V_SET_CONN, path[i].conn_index);
        if (wcap & WCAP_AMP_OUT)
            set_amp(codec, nid, wcap, 1, 0);
        if (!last && (wcap & WCAP_AMP_IN))
            set_amp(codec, nid, wcap, 0, type == WIDGET_MIXER || type == WIDGET_SEL ? path[i].conn_index : 0);
    }
}

static BOOLEAN setup_function_group(UINT32 codec, UINT32 afg, UINT16 format)
{
    UINT32 nodes = param(codec, afg, P_NODE_COUNT);
    UINT32 first = (nodes >> 16) & 0xFF, count = nodes & 0xFF;
    UINT32 dac = 0;

    hda.afg = (UINT8)afg;
    hda.afg_amp_in = param(codec, afg, P_AMP_IN);
    hda.afg_amp_out = param(codec, afg, P_AMP_OUT);
    verb(codec, afg, V_SET_POWER, 0);
    gBS->Stall(1000);

    for (UINT32 nid = first; nid < first + count; nid++) {
        struct path_hop path[MAX_PATH];
        UINTN length = 0;
        UINT32 config, device, pin_caps;

        if (WIDGET_TYPE(param(codec, nid, P_WIDGET_CAP)) != WIDGET_PIN)
            continue;
        pin_caps = param(codec, nid, P_PIN_CAP);
        config = verb(codec, nid, V_GET_CFG, 0);
        device = (config >> 20) & 0xF;
        if (!(pin_caps & PINCAP_OUT) || (config >> 30) == 1 || device > 2)
            continue; /* 0 line out, 1 speaker, 2 headphones */
        if (!trace_to_dac(codec, nid, 0, path, &length, &dac))
            continue;

        enable_path(codec, path, length);
        verb(codec, nid, V_SET_PIN, 0x40u | (pin_caps & PINCAP_HP ? 0x80u : 0));
        if (pin_caps & PINCAP_EAPD)
            verb(codec, nid, V_SET_EAPD, 0x02);
    }
    if (!dac)
        return 0;

    verb4(codec, dac, V_SET_FORMAT_4, format);
    verb(codec, dac, V_SET_STREAM, STREAM_TAG << 4);
    return 1;
}

static BOOLEAN setup_codecs(UINT16 format)
{
    UINT16 present;

    /* Give slow codecs up to the full timeout to appear. */
    for (int i = 0; i < TIMEOUT_POLLS && !(present = rd16(STATESTS)); i++)
        gBS->Stall(10);

    for (UINT32 codec = 0; codec < 15; codec++) {
        UINT32 nodes;

        if (!(present & (1u << codec)))
            continue;
        nodes = param(codec, 0, P_NODE_COUNT);
        for (UINT32 nid = (nodes >> 16) & 0xFF, end = nid + (nodes & 0xFF); nid < end; nid++) {
            if ((param(codec, nid, P_FUNC_TYPE) & 0xFF) == FUNC_AUDIO &&
                setup_function_group(codec, nid, format))
                return 1;
        }
    }
    return 0;
}

/* ---- Stream ----------------------------------------------------------- */

static EFI_STATUS encode_format(const struct audio_format *f, UINT16 *out)
{
    static const struct { UINT32 rate; UINT16 bits; } rates[] = {
        {48000, 0x0000}, {44100, 0x4000}, {96000, 0x0800},
        {88200, 0x4800}, {24000, 0x0100}, {22050, 0x4100},
    };

    if (f->bits != 16 || f->channels < 1 || f->channels > 8)
        return EFI_UNSUPPORTED;
    for (UINTN i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        if (rates[i].rate == f->rate) {
            *out = rates[i].bits | (1u << 4) | (UINT16)(f->channels - 1);
            return EFI_SUCCESS;
        }
    }
    return EFI_UNSUPPORTED;
}

static EFI_STATUS setup_stream(UINT16 format, UINT32 ring_bytes)
{
    struct bdl_entry *bdl = (struct bdl_entry *)((UINT8 *)hda.commands.host + DMA_BDL_OFFSET);
    UINT64 bdl_device = hda.commands.device + DMA_BDL_OFFSET;
    UINT32 segment = ring_bytes / BDL_ENTRIES;
    UINT32 sd = hda.stream;

    wr8(sd + SD_CTL0, rd8(sd + SD_CTL0) & (UINT8)~SD_CTL_RUN);
    wait8(sd + SD_CTL0, SD_CTL_RUN, 0);
    wr8(sd + SD_CTL0, SD_CTL_SRST);
    if (!wait8(sd + SD_CTL0, SD_CTL_SRST, SD_CTL_SRST))
        return EFI_DEVICE_ERROR;
    gBS->Stall(10);
    wr8(sd + SD_CTL0, 0);
    if (!wait8(sd + SD_CTL0, SD_CTL_SRST, 0))
        return EFI_DEVICE_ERROR;

    for (UINT32 i = 0; i < BDL_ENTRIES; i++) {
        bdl[i].address = hda.ring.device + i * segment;
        bdl[i].length = segment;
        bdl[i].flags = 0;
    }

    wr32(sd + SD_CBL, segment * BDL_ENTRIES);
    wr16(sd + SD_LVI, BDL_ENTRIES - 1);
    wr16(sd + SD_FMT, format);
    wr32(sd + SD_BDPL, (UINT32)bdl_device);
    wr32(sd + SD_BDPU, (UINT32)(bdl_device >> 32));
    wr8(sd + SD_CTL2, STREAM_TAG << 4);
    return EFI_SUCCESS;
}

/* ---- Driver interface ------------------------------------------------- */

static void release(void)
{
    if (hda.stream) {
        wr8(hda.stream + SD_CTL0, 0);
        wait8(hda.stream + SD_CTL0, SD_CTL_RUN, 0);
        hda.stream = 0;
    }
    if (hda.commands.pages)
        stop_command_rings();
    dma_free(&hda.ring);
    dma_free(&hda.commands);
}

static EFI_STATUS try_controller(EFI_PCI_IO_PROTOCOL *pci, UINT16 format, UINT32 ring_bytes, UINT8 **ring)
{
    UINT64 supported = 0;
    UINT16 gcap;
    EFI_STATUS status;

    memset(&hda, 0, sizeof(hda));
    hda.pci = pci;

    status = pci->Attributes(pci, EfiPciIoAttributeOperationSupported, 0, &supported);
    if (EFI_ERROR(status))
        return status;
    status = pci->Attributes(pci, EfiPciIoAttributeOperationEnable,
                             supported & (EFI_PCI_IO_ATTRIBUTE_MEMORY | EFI_PCI_IO_ATTRIBUTE_BUS_MASTER), 0);
    if (EFI_ERROR(status))
        return status;

    status = dma_alloc(&hda.commands, 1);
    if (EFI_ERROR(status))
        return status;
    status = dma_alloc(&hda.ring, (ring_bytes + 4095) / 4096);
    if (EFI_ERROR(status))
        goto fail;

    status = reset_controller();
    if (EFI_ERROR(status))
        goto fail;

    gcap = rd16(GCAP);
    if (((gcap >> 12) & 0xF) == 0) { /* no output streams */
        status = EFI_UNSUPPORTED;
        goto fail;
    }

    start_command_rings();
    if (!setup_codecs(format)) {
        status = EFI_NOT_FOUND;
        goto fail;
    }

    /* Output stream descriptors follow the input ones. */
    hda.stream = 0x80 + ((gcap >> 8) & 0xF) * 0x20;
    status = setup_stream(format, ring_bytes);
    if (EFI_ERROR(status))
        goto fail;

    *ring = hda.ring.host;
    return EFI_SUCCESS;

fail:
    release();
    return status;
}

static EFI_STATUS hda_open(const struct audio_format *format, UINT32 ring_bytes, UINT8 **ring)
{
    static const EFI_GUID pci_io_guid = EFI_PCI_IO_PROTOCOL_GUID;
    EFI_HANDLE *handles;
    UINTN count;
    UINT16 encoded;
    EFI_STATUS status = encode_format(format, &encoded), result = EFI_NOT_FOUND;

    if (EFI_ERROR(status))
        return status;
    if (ring_bytes % BDL_ENTRIES)
        return EFI_INVALID_PARAMETER;

    status = gBS->LocateHandleBuffer(ByProtocol, &pci_io_guid, 0, &count, &handles);
    if (EFI_ERROR(status))
        return status;

    for (UINTN i = 0; i < count; i++) {
        EFI_PCI_IO_PROTOCOL *pci;
        UINT8 class_code[3]; /* prog-if, subclass, base class */

        if (EFI_ERROR(gBS->HandleProtocol(handles[i], &pci_io_guid, (void **)&pci)))
            continue;
        if (EFI_ERROR(pci->Pci.Read(pci, EfiPciIoWidthUint8, 0x09, 3, class_code)))
            continue;
        if (class_code[2] != 0x04 || class_code[1] != 0x03)
            continue;

        result = try_controller(pci, encoded, ring_bytes, ring);
        if (!EFI_ERROR(result))
            break;
    }
    gBS->FreePool(handles);
    return result;
}

static void hda_start(void)
{
    wr8(hda.stream + SD_CTL0, rd8(hda.stream + SD_CTL0) | SD_CTL_RUN);
}

static UINT32 hda_position(void)
{
    return rd32(hda.stream + SD_LPIB);
}

const struct audio_driver hda_driver = {
    .name = L"Intel HD Audio",
    .open = hda_open,
    .start = hda_start,
    .position = hda_position,
    .close = release,
};
