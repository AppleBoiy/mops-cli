import subprocess
import pytest
import sys

# Helper function to run the CLI
def run_mops(*args, timeout=2):
    try:
        result = subprocess.run(
            ["./mops", *args],
            capture_output=True,
            text=True,
            timeout=timeout
        )
        return result
    except subprocess.TimeoutExpired as e:
        return e

class TestBaseCommands:
    def test_no_args_fails(self):
        res = run_mops()
        assert res.returncode == 1
        assert "Usage: " in res.stdout or "Usage: " in res.stderr

    def test_help(self):
        res = run_mops("--help")
        assert res.returncode == 0
        assert "mops - Multipurpose Operations CLI" in res.stdout

    def test_author(self):
        res = run_mops("--author")
        assert res.returncode == 0
        assert "Personal Details" in res.stdout

    def test_invalid_command(self):
        res = run_mops("invalid_command")
        assert res.returncode == 1
        assert "Unknown command:" in res.stderr

@pytest.mark.skipif(sys.platform != "linux", reason="Requires Linux /proc filesystem")
class TestDiskModule:
    def test_disk_no_args(self):
        res = run_mops("disk")
        assert res.returncode == 1
        assert "Usage: mops disk" in res.stderr

    def test_disk_help(self):
        res = run_mops("disk", "--help")
        assert res.returncode == 0
        assert "Commands:" in res.stdout

    @pytest.mark.parametrize("subcmd", ["status", "usage", "mounts"])
    def test_disk_subcommands(self, subcmd):
        res = run_mops("disk", subcmd)
        assert res.returncode == 0

    @pytest.mark.parametrize("subcmd", ["status", "usage", "mounts"])
    def test_disk_flags(self, subcmd):
        res = run_mops("disk", subcmd, "-h", "-l")
        assert res.returncode == 0

@pytest.mark.skipif(sys.platform != "linux", reason="Requires Linux /proc filesystem")
class TestSysModule:
    def test_sys_no_args(self):
        res = run_mops("sys")
        assert res.returncode == 1
        assert "Usage: mops sys" in res.stderr

    def test_sys_help(self):
        res = run_mops("sys", "--help")
        assert res.returncode == 0

    @pytest.mark.parametrize("args", [
        ["cpu"], ["cpu", "-h"], ["cpu", "-l"], ["cpu", "-hl"],
        ["mem"], ["mem", "-h"], ["mem", "--json"],
        ["gpu"], ["gpu", "-hl"], ["gpu", "--pids"],
        ["tpu"], ["tpu", "-hl"]
    ])
    def test_sys_informational(self, args):
        res = run_mops("sys", *args)
        assert res.returncode == 0

    @pytest.mark.parametrize("args", [
        ["oom"], ["cgroup"]
    ])
    def test_sys_privileged_allow_fail(self, args):
        res = run_mops("sys", *args)
        # These might fail on unprivileged systems or macOS, which is expected
        assert res.returncode in (0, 1)

@pytest.mark.skipif(sys.platform != "linux", reason="Requires Linux /proc filesystem")
class TestAliases:
    def test_mem_alias(self):
        res1 = run_mops("sys", "mem", "-h")
        res2 = run_mops("mem", "-h")
        assert res1.returncode == 0
        assert res2.returncode == 0
        # Output should be very similar (though dynamic, formats match)
        assert "System Memory:" in res1.stdout
        assert "System Memory:" in res2.stdout

class TestNetModule:
    def test_net_no_args(self):
        res = run_mops("net")
        assert res.returncode == 1

    @pytest.mark.parametrize("port", ["22", "80"])
    def test_net_port(self, port):
        res = run_mops("net", "port", port)
        assert res.returncode == 0

    def test_net_invalid_port(self):
        res = run_mops("net", "port", "999999")
        assert res.returncode == 1

class TestDashboard:
    @pytest.mark.skipif(sys.platform != "linux", reason="ncurses best tested natively or manually")
    def test_dashboard_blocks(self):
        # The dashboard launches ncurses and waits for 'q'.
        # We test that it launches successfully and blocks (times out).
        res = run_mops("dashboard", timeout=1)
        assert isinstance(res, subprocess.TimeoutExpired)
