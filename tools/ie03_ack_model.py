#!/usr/bin/env python3
"""IE-03 design-only, deterministic ACK/commit/generation reference model.

IMPORTANT: NOT A STORAGE ENGINE, NOT DURABLE, NOT A GPU/SWAP BACKEND.
The journal here is an ordinary in-memory Python list. Its append is modeled
as one atomic durable commit, NOT a property implemented by any filesystem.
Use only to validate the *contract* that IE-06 must later implement and test
with actual stable-storage fault injection and ublk lifecycle integration.

No subprocess, files, OpenCL, ublk, root, /proc, or kernel SWAP operations.
"""
from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from hashlib import sha256

PAGE_BYTES = 4096
MAX_IO_BYTES = 65536


class ContractError(Exception):
    """Invalid request, ordering error or unsafe state. Never report ACK."""


class CorruptAuthority(ContractError):
    """No verified authoritative bytes exist for a requested page."""


class Phase(Enum):
    PREPARED = auto()
    DATA_DURABLE = auto()


@dataclass(frozen=True)
class Commit:
    """A simulated *atomic* metadata commit referencing already durable data.

    In production, checksums, immutable extents, journal transaction ID,
    journal sequence and boot epoch must be persisted and independently
    verified. This mock embeds data in the commit for deterministic replay.
    """
    request_id: str
    boot_epoch: str
    first_page: int
    generations: tuple[int, ...]
    page_digests: tuple[str, ...]
    payload: bytes


@dataclass
class Pending:
    request_id: str
    first_page: int
    payload: bytes
    phase: Phase = Phase.PREPARED


def digest(page: bytes) -> str:
    return sha256(page).hexdigest()


class PageContractModel:
    """Reference state machine; immutable authoritative writes, GPU=cache.

    Only a committed record (not GPU completion, buffered disk write,
    speculative page allocation, or kernel timer expiry) permits WRITE ACK.
    Each commit atomically publishes a multi-page request and monotonically
    increments per-page generations. Overlapping pending requests are refused.
    """

    def __init__(self, capacity_pages: int, boot_epoch: str,
                 journal: tuple[Commit, ...] = ()) -> None:
        if not isinstance(capacity_pages, int) or capacity_pages < 1:
            raise ContractError("invalid capacity")
        if not boot_epoch:
            raise ContractError("boot epoch is mandatory")
        self.capacity_pages = capacity_pages
        self.boot_epoch = boot_epoch
        self.journal = list(journal)  # Not actually durable. Model only!
        self.authority: dict[int, tuple[int, bytes, str]] = {}
        self.committed_ids: dict[str, tuple[int, str]] = {}
        self.pending: dict[str, Pending] = {}
        self.gpu_cache: dict[int, tuple[int, int, bytes, str]] = {}
        self.gpu_epoch = 1
        self._replay()

    def _validate_range(self, first_page: int, size: int) -> int:
        if not isinstance(first_page, int) or first_page < 0:
            raise ContractError("invalid first page")
        if size < PAGE_BYTES or size > MAX_IO_BYTES or size % PAGE_BYTES:
            raise ContractError("I/O must be 4KiB-aligned, 4..64KiB")
        count = size // PAGE_BYTES
        if first_page + count > self.capacity_pages:
            raise ContractError("out of bounds")
        return count

    def _replay(self) -> None:
        """Reject a torn/incorrect journal; never guess a lost generation."""
        for record in self.journal:
            if record.boot_epoch != self.boot_epoch:
                raise ContractError("cross-boot restore forbidden")
            count = self._validate_range(record.first_page,
                                         len(record.payload))
            if not record.request_id or record.request_id in self.committed_ids:
                raise ContractError("duplicate committed transaction")
            if len(record.generations) != count or len(record.page_digests) != count:
                raise CorruptAuthority("invalid commit manifest")
            for n in range(count):
                page_id = record.first_page + n
                page = record.payload[n*PAGE_BYTES:(n+1)*PAGE_BYTES]
                previous = self.authority.get(page_id, (0, b"", ""))[0]
                if record.generations[n] != previous + 1:
                    raise CorruptAuthority("generation sequence broken")
                if record.page_digests[n] != digest(page):
                    raise CorruptAuthority("committed data checksum incorrect")
            for n in range(count):
                idx = record.first_page + n
                chunk = record.payload[n*PAGE_BYTES:(n+1)*PAGE_BYTES]
                self.authority[idx] = (
                    record.generations[n], chunk, record.page_digests[n]
                )
            self.committed_ids[record.request_id] = (
                record.first_page, digest(record.payload)
            )

    def begin_write(self, request_id: str, first_page: int,
                    payload: bytes) -> str:
        if not request_id or not isinstance(payload, bytes):
            raise ContractError("request ID and immutable payload required")
        count = self._validate_range(first_page, len(payload))
        identity = (first_page, digest(payload))
        if request_id in self.committed_ids:
            if self.committed_ids[request_id] != identity:
                raise ContractError("same request ID with different content")
            return "ALREADY_COMMITTED"
        if request_id in self.pending:
            old = self.pending[request_id]
            if (old.first_page, digest(old.payload)) != identity:
                raise ContractError("conflicting pending retry")
            return "ALREADY_PENDING"
        new_pages = set(range(first_page, first_page + count))
        for pending in self.pending.values():
            old_count = len(pending.payload) // PAGE_BYTES
            if new_pages.intersection(range(pending.first_page,
                                            pending.first_page + old_count)):
                raise ContractError("overlapping writes need serialization")
        self.pending[request_id] = Pending(request_id, first_page, payload)
        return "PREPARED"

    def persist_cow_data(self, request_id: str) -> None:
        """Model an immutable new extent becoming durable, NOT a real fsync."""
        p = self.pending.get(request_id)
        if p is None or p.phase is not Phase.PREPARED:
            raise ContractError("data must be prepared exactly once")
        p.phase = Phase.DATA_DURABLE

    def commit_manifest(self, request_id: str) -> Commit:
        """Simulated journal commit *after* COW data persistence.

        Actual atomic journal records, ordering barriers, fsync and recovery
        parsing are implementation tasks for IE-06, NOT provided by this file.
        """
        p = self.pending.get(request_id)
        if p is None or p.phase is not Phase.DATA_DURABLE:
            raise ContractError("cannot commit non-durable data")
        count = len(p.payload) // PAGE_BYTES
        generations = []
        digests = []
        for n in range(count):
            idx = p.first_page + n
            generations.append(self.authority.get(idx, (0, b"", ""))[0] + 1)
            digests.append(digest(p.payload[n*PAGE_BYTES:(n+1)*PAGE_BYTES]))
        record = Commit(request_id=request_id, boot_epoch=self.boot_epoch,
                        first_page=p.first_page,
                        generations=tuple(generations),
                        page_digests=tuple(digests), payload=p.payload)
        # This append is ATOMIC IN THE MODEL. It is NOT an I/O operation.
        self.journal.append(record)
        for n in range(count):
            idx = p.first_page + n
            page = p.payload[n*PAGE_BYTES:(n+1)*PAGE_BYTES]
            self.authority[idx] = (generations[n], page, digests[n])
            self.gpu_cache.pop(idx, None)
        self.committed_ids[request_id] = (
            p.first_page, digest(p.payload)
        )
        del self.pending[request_id]
        return record

    def ack_write(self, request_id: str) -> bool:
        """Only committed requests can be ACKed, incl. identical retries."""
        if request_id not in self.committed_ids:
            raise ContractError("NO ACK before durable manifest commit")
        return True

    def put_gpu_cache(self, page_id: int, generation: int,
                      gpu_epoch: int, data: bytes) -> bool:
        """Discard late GPU responses belonging to old page/gen/epoch."""
        self._validate_range(page_id, len(data))
        if len(data) != PAGE_BYTES:
            raise ContractError("GPU cache is page-granular")
        item = self.authority.get(page_id)
        if item is None:
            return False
        if (gpu_epoch != self.gpu_epoch or generation != item[0] or
                digest(data) != item[2]):
            return False
        self.gpu_cache[page_id] = (generation, gpu_epoch, data, item[2])
        return True

    def gpu_reset(self) -> None:
        """No authoritative data changes when GPU cache vanishes."""
        self.gpu_epoch += 1
        self.gpu_cache.clear()

    def read(self, first_page: int, page_count: int = 1) -> bytes:
        self._validate_range(first_page, page_count * PAGE_BYTES)
        result = bytearray()
        for idx in range(first_page, first_page + page_count):
            generation, body, checksum = self.authority.get(
                idx, (0, bytes(PAGE_BYTES), digest(bytes(PAGE_BYTES)))
            )
            if digest(body) != checksum:
                raise CorruptAuthority("authoritative page corrupt")
            cached = self.gpu_cache.get(idx)
            if cached is not None:
                if cached[0] != generation or cached[1] != self.gpu_epoch:
                    self.gpu_cache.pop(idx, None)
                elif cached[3] != checksum or digest(cached[2]) != checksum:
                    self.gpu_cache.pop(idx, None)
                else:
                    body = cached[2]
            result.extend(body)
        return bytes(result)

    def read_into(self, first_page: int, page_count: int,
                  dst: bytearray) -> None:
        """Caller buffer is mutated only after *all* pages validate."""
        expected_len = page_count * PAGE_BYTES
        if len(dst) != expected_len:
            raise ContractError("buffer size mismatch")
        verified = self.read(first_page, page_count)
        dst[:] = verified

    def simulate_same_boot_restart(self) -> PageContractModel:
        """Replay only committed journal transactions, discard pending data."""
        return PageContractModel(self.capacity_pages, self.boot_epoch,
                                 tuple(self.journal))

    def simulate_authority_corruption(self, page_id: int) -> None:
        """Fault injection in memory only, never touches a file or device."""
        item = self.authority.get(page_id)
        if item is None:
            raise ContractError("page has no committed authority")
        altered = bytes([item[1][0] ^ 1]) + item[1][1:]
        self.authority[page_id] = (item[0], altered, item[2])
