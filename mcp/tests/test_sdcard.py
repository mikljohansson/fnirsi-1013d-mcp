"""SD sector commands (fw 0.2.2, section 14 row 40) in the simulator and fnirsi1013d-flash --persist / --restore."""

from __future__ import annotations

import hashlib
from pathlib import Path

import pytest

from fnirsi1013d import flash as F
from fnirsi1013d import protocol as P
from fnirsi1013d.sdcard import SdCard, SdError, check_image
from fnirsi1013d.simulator import make_egon_image
from fnirsi1013d.transport import DeviceError, Transport, open_link

from test_flash import FakeHost


def test_sd_info_read_and_stage(sim, transport):
    sd = SdCard(transport)
    info = sd.info(verify=True)
    assert info.card_sectors == 131072 and info.part_start == 8192 and info.write_end == 2048
    assert [(h.sector, h.magic, h.check) for h in info.headers] == [(16, 1, 1), (80, 2, 1), (750, 2, 1)]
    assert sd.info().header(750).check == P.SD_CHECK_NOT
    assert sd.read(750, 300)[:12] == make_egon_image(300 * 1024, b"eGON.EXE", 0x75)[:12]
    sd.stage(bytes(range(256)) * 10)
    assert bytes(sim.device.sd_stage[:2560]) == bytes(range(256)) * 10
    with pytest.raises(DeviceError) as e:  # length field does not match the payload
        transport.request(P.Cmd.SD_STAGE, P.SdStageHeader(0, 10).pack() + b"x" * 9, retries=0)
    assert (e.value.status, e.value.detail) == (P.Status.E_BAD_LEN, 4)
    with pytest.raises(DeviceError) as e:
        transport.request(P.Cmd.SD_STAGE, P.SdStageHeader(65530, 10).pack() + b"x" * 10, retries=0)
    assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 0)
    with pytest.raises(DeviceError) as e:
        transport.request(P.Cmd.SD_READ_SECTORS, P.SdReadReq(0, 129).pack(), retries=0)
    assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 4)


@pytest.mark.parametrize("sector,count,flags,crc_ok,magic,expect", [
    (750, 1, 0, True, 0x1234, (P.Status.E_BAD_ARG, 12)),
    (750, 1, 0x04, True, None, (P.Status.E_BAD_ARG, 6)),
    (750, 0, 0, True, None, (P.Status.E_BAD_ARG, 4)),
    (15, 1, P.SD_WR_BOOT, True, None, (P.Status.E_BAD_ARG, 0)),
    (2047, 2, 0, True, None, (P.Status.E_BAD_ARG, 0)),
    (40, 1, 0, True, None, (P.Status.E_BAD_ARG, 0)),  # 16-79 need BOOT
    (700, 16, 0, True, None, (P.Status.E_BAD_ARG, 0)),  # covers 708-710: needs CONFIG
    (750, 1, 0, False, None, (P.Status.E_BAD_ARG, 8)),
    (700, 16, P.SD_WR_CONFIG, True, None, None),
    (40, 1, P.SD_WR_BOOT, True, None, None),
])
def test_sd_write_refusals_in_firmware_order(sim, transport, sector, count, flags, crc_ok, magic, expect):
    SdCard(transport).stage(bytes(P.SD_STAGE_MAX_CHUNK) * 2)
    data = bytes(sim.device.sd_stage[: count * 512])
    crc = P.crc32_iso_hdlc(data) if crc_ok else 1
    req = P.SdWriteReq(sector, count, flags, crc, P.SD_WRITE_MAGIC if magic is None else magic)
    if expect is None:
        r = P.SdWriteResp.unpack(transport.request(P.Cmd.SD_WRITE_SECTORS, req.pack(), retries=0).payload)
        assert r.crc32 == crc and sim.device.sd.writes == [(sector, count)]
        return
    with pytest.raises(DeviceError) as e:
        transport.request(P.Cmd.SD_WRITE_SECTORS, req.pack(), retries=0)
    assert (e.value.status, e.value.detail) == expect and sim.device.sd.writes == []


def test_sd_write_without_mbr_card_and_io_errors(sim, transport):
    sd = SdCard(transport)
    sd.stage(bytes(512))
    dev = sim.device
    dev.sd.io_error_next = -2
    with pytest.raises(DeviceError) as e:
        sd.write_staged(750, bytes(512))
    assert (e.value.status, e.value.detail) == (P.Status.E_IO, 2)
    dev.sd.fail_next_write = 1
    with pytest.raises(DeviceError) as e:
        sd.write_staged(750, bytes(512))
    assert (e.value.status, e.value.detail) == (P.Status.E_IO, 0xFFFF)
    dev.sd.put(0, bytes(512))  # MBR gone
    with pytest.raises(DeviceError) as e:
        sd.write_staged(750, bytes(512))
    assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 0)
    dev.sd.present = False
    with pytest.raises(DeviceError) as e:
        sd.info()
    assert e.value.status == P.Status.E_IO


def test_lost_write_answer_is_not_written_twice(sim, transport):
    sd = SdCard(transport)
    data = bytes(range(256)) * 4
    sd.stage(data)
    orig = sim._send

    def drop_first_write_answer(client, resp, state={"n": 0}):
        if resp.cmd == P.Cmd.SD_WRITE_SECTORS and not state["n"]:
            state["n"] = 1
            return
        orig(client, resp)
    sim._send = drop_first_write_answer
    transport.response_timeout = 0.3
    sd.write_staged(750, data)  # resent with the same seq: answered from the duplicate cache
    assert sim.device.sd.writes == [(750, 2)]


def test_check_image():
    good = make_egon_image(5000)
    assert check_image(good)[0] == 5120
    bad = bytearray(good)
    bad[600] ^= 1
    with pytest.raises(SdError, match="checksum"):
        check_image(bytes(bad))
    with pytest.raises(SdError, match="eGON.EXE"):
        check_image(make_egon_image(1024, b"eGON.BT0"))


def persister(srv, tmp_path, image: bytes, **kw):
    img = tmp_path / "fnirsi_1013d.bin"
    img.write_bytes(image)
    lines: list[str] = []
    kw.setdefault("persist_only", True)
    f = F.Flasher(port=srv.url, image=img, windows=True, runner=FakeHost(), out=lines.append,
                  transport_factory=lambda url: Transport(open_link(url)), sleep=lambda s: None, fel_timeout=5,
                  port_timeout=5, backup_dir=tmp_path / "backups", **kw)
    return f, lines


def test_persist_only_writes_backs_up_and_verifies(sim, tmp_path):
    card = sim.device.sd
    old = card.read(750, 2048 - 750)
    image = make_egon_image(200 * 1024, fill=0x11)
    f, lines = persister(sim, tmp_path, image)
    assert f.run_all() == 0, "\n".join(lines)
    assert card.read(750, len(image) // 512) == image
    assert [w for w in card.writes] == [(750 + 128 * k, 128) for k in range(3)] + [(750 + 384, 16)]
    backups = list((tmp_path / "backups").glob("sd_750-2048_*.bin"))
    assert len(backups) == 1 and backups[0].read_bytes() == old
    text = "\n".join(lines)
    assert hashlib.sha256(old).hexdigest() in text and "verified on the card" in text and "FAILED" not in text


def test_full_flow_then_persist(sim, tmp_path):
    image = make_egon_image(64 * 1024 + 512, fill=0x22)
    f, lines = persister(sim, tmp_path, image, persist_only=False, persist=True)
    assert f.run_all() == 0, "\n".join(lines)
    text = "\n".join(lines)
    assert text.index("scope back on") < text.index("backup of sectors") < text.index("verified on the card")
    assert sim.device.sd.read(750, len(image) // 512) == image


def test_persist_mismatch_then_restore(sim, tmp_path):
    card = sim.device.sd
    old = card.read(750, 2048 - 750)
    card.fail_next_write = 1
    f, lines = persister(sim, tmp_path, make_egon_image(100 * 1024, fill=0x33))
    assert f.run_all() == 7
    failed = [ln for ln in lines if "FAILED" in ln]
    assert len(failed) == 1 and "E_IO" in failed[0]
    backup = next((tmp_path / "backups").glob("sd_750-2048_*.bin"))
    r, lines2 = persister(sim, tmp_path, b"", restore=backup)
    assert r.run_all() == 0, "\n".join(lines2)
    assert card.read(750, 2048 - 750) == old and any("restore verified" in ln for ln in lines2)


def test_persist_refusals(sim_factory, tmp_path):
    srv = sim_factory()
    bad = bytearray(make_egon_image(4096))
    bad[1000] ^= 1
    f, lines = persister(srv, tmp_path, bytes(bad))
    assert f.run_all() == 2 and any("checksum" in ln for ln in lines) and srv.device.sd.writes == []
    legacy = sim_factory(legacy=True)
    f, lines = persister(legacy, tmp_path, make_egon_image(4096))
    assert f.run_all() == 7 and any("no SD sector commands" in ln for ln in lines)
    big = make_egon_image(700 * 1024)  # 1400 sectors: 750 + 1400 > 2048
    f, lines = persister(srv, tmp_path, big)
    assert f.run_all() == 7 and any("does not fit" in ln for ln in lines) and srv.device.sd.writes == []
    assert not list((tmp_path / "backups").glob("*.bin"))  # nothing backed up for refused runs
    r, lines = persister(srv, tmp_path, b"", restore=Path(tmp_path / "x.bin"))
    assert r.run_all() == 2
