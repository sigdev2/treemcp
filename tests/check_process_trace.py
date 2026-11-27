"""Trace a Release server from startup to EOF during the read-only five-tool fixture."""

import argparse
from pathlib import Path
import os
import re

from check_read_only import drop_write_override, exercise


def linux_trace(directory, sizes):
    # strace filters file, descriptor, network and process calls. An unfamiliar call in
    # those classes needs review before a new runtime can qualify.
    reads = images = 0
    writes = []
    harmless = set("access arch_prctl brk close epoll_create1 epoll_ctl epoll_wait eventfd2 exit "
                   "exit_group faccessat faccessat2 fstat fstatfs getcwd getdents64 getpid "
                   "getppid gettid lseek newfstatat poll ppoll pread64 readlink readlinkat "
                   "select pselect6 stat statfs statx".split())
    for path in directory.glob("strace.*"):
        lines = path.read_text(errors="replace").splitlines()
        assert lines and lines[-1] == "+++ exited with 0 +++", path
        for line in lines:
            call = line.partition("(")[0]
            if call in harmless or line.startswith(("--- ", "+++ ")):
                continue
            if call == "execve":
                images += 1
            elif call in ("clone", "clone3"):
                assert "CLONE_THREAD" in line, line
            elif call in ("open", "openat", "openat2"):
                assert not re.search(r"O_(WRONLY|RDWR|CREAT|TRUNC|TMPFILE|APPEND)\b", line), line
            elif call == "mmap":
                assert not ("PROT_WRITE" in line and "MAP_SHARED" in line and "MAP_ANONYMOUS" not in line), line
            elif call == "fcntl":
                assert re.search(r", F_(GETFD|SETFD|GETFL|DUPFD_CLOEXEC)\b", line), line
            elif call == "read":
                if re.match(r"read\(\d+<[^>]+/acceptance_fixture\.py>", line) and line.endswith(" = 13"):
                    reads += 1
            elif call == "write":
                if re.match(r"write\(\d+<anon_inode:\[eventfd\]>", line):
                    continue
                assert line.startswith("write(1<pipe:["), line
                writes.append(int(line.rsplit(" = ", 1)[1]))
            else:
                raise AssertionError(line)
    assert images == 1 and reads == 5 and writes == sizes, (images, reads, writes, sizes)


def windows_trace(path, sizes):
    lines = path.read_text(errors="replace").splitlines()
    processes = [line for line in lines if re.match(r"Process \d+ starting", line)]
    assert len(processes) == 1 and lines[-1] == processes[0].split(" starting")[0] + " exit code: 0"
    writes = []
    output_handles = set()
    fixture_handles = set()
    reads = 0
    for line in lines:
        call = line.partition("(")[0]
        if call in ("NtOpenFile", "NtCreateFile"):
            name = re.search(r'ObjectAttributes=(?:\w+:)?"([^"]*)"', line)[1].lower()
            assert not re.search(r"\\(?:device\\(?:afd|tcp6?|udp6?|nsi|mup|lanmanredirector)|\?\?\\unc)(?:\\|$)", name), line
            # Console initialization and the Windows security provider open kernel devices.
            if name in (r"\connect", r"\device\condrv\connect", r"\device\ksecdd"):
                continue
            access = re.search(r"DesiredAccess=([^,]+)", line)[1].split("|")
            for right in access:
                if right in ("SYNCHRONIZE", "READ_CONTROL", "GENERIC_READ", "GENERIC_EXECUTE"):
                    continue
                # READ_DATA, READ_EA, EXECUTE/TRAVERSE and READ_ATTRIBUTES.
                assert right.startswith("0x") and not int(right, 16) & ~0xA9, line
            if call == "NtCreateFile":
                assert ", CreateDisposition=1," in line, line  # FILE_OPEN
                options = int(re.search(r"CreateOptions=(\w+)", line)[1], 0)
                assert not options & 0x1000, line  # FILE_DELETE_ON_CLOSE
            if name == "acceptance_fixture.py" and line.endswith(" => 0"):
                fixture_handles.add(re.search(r"FileHandle=\w+ \[(\w+)\]", line)[1])
        elif call == "NtClose":
            fixture_handles.discard(re.search(r"Handle=(\w+)", line)[1])
        elif call == "NtReadFile":
            handle = re.search(r"FileHandle=(\w+)", line)[1]
            if handle in fixture_handles and "[0/0xd]" in line and line.endswith(" => 0"):
                reads += 1
        elif call == "NtWriteFile":
            output_handles.add(re.search(r"FileHandle=(\w+)", line)[1])
            assert line.endswith(" => 0"), line
            writes.append(int(re.search(r"IoStatusBlock=\w+ \[0/(\w+)\]", line)[1], 0))
        elif call == "NtSetInformationFile":
            assert "[FilePositionInformation]" in line, line
        elif call == "NtDeviceIoControlFile":
            # Runtime RNG, console initialization/notification and mount point queries.
            code = int(re.search(r"IoControlCode=(\w+)", line)[1], 0)
            assert code in (0x390008, 0x390400, 0x500016, 0x500023, 0x6D0030), line
        elif call.startswith(("NtCreateProcess", "NtCreateUserProcess", "NtSetValueKey",
                              "NtDeleteKey", "NtDeleteValueKey", "NtWriteFile")):
            raise AssertionError(line)
        elif call.startswith("Nt") and call.endswith("File"):
            assert call.startswith("NtQuery"), line
    # Match every native write on one handle to the frames received through the stdout pipe.
    assert reads == 5 and len(output_handles) == 1 and writes == sizes, (reads, writes, sizes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("directory", type=Path, help="new evidence directory")
    parser.add_argument("--nttrace", type=Path, help="NtTrace64.exe v1.1 beside NtTrace.cfg")
    args = parser.parse_args()
    directory = args.directory.resolve()
    directory.mkdir(parents=True)
    if os.name == "nt":
        if not args.nttrace:
            parser.error("Windows tracing requires --nttrace")
        tracer = args.nttrace.resolve()
        path = directory / "native.log"
        prefix = [tracer, "-config", tracer.with_name("NtTrace.cfg"), "-hd", "-out", path]
    else:
        prefix = ["strace", "-ff", "-yy", "-s", "256", "-e", "trace=%file,%desc,%network,%process,mmap",
                  "-o", directory / "strace"]
    sizes = exercise(args.program.resolve(), prefix)
    if os.name == "nt":
        windows_trace(path, sizes)
    else:
        linux_trace(directory, sizes)
    print(f"{os.name}: one server, five fixture reads, {len(sizes)} protocol writes, "
          "zero filesystem mutations, child processes or network activity in trace")


if __name__ == "__main__":
    drop_write_override()
    main()
