import json
import os
import platform
import re
import shutil
import subprocess
import time
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[1]
MOPS_BIN = PROJECT_ROOT / "mops"


def run_cmd(args, cwd=None, env=None, check=True, timeout=30):
    """
    Run a command (list of args). Returns (exit_code, stdout, stderr).
    """
    proc = subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        text=True,
    )
    if check and proc.returncode != 0:
        raise AssertionError(
            f"Command failed: {args}\nExit: {proc.returncode}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
        )
    return proc.returncode, proc.stdout, proc.stderr


@pytest.fixture(scope="session")
def build_binary():
    # Ensure the binary is built before tests
    if shutil.which("make") is None:
        pytest.skip("make not available to build mops")
    # Clean and build
    run_cmd(["make", "clean"], cwd=PROJECT_ROOT, check=False)
    run_cmd(["make", "-j4"], cwd=PROJECT_ROOT)
    assert MOPS_BIN.exists(), "mops binary was not built"
    return str(MOPS_BIN)


@pytest.fixture
def temp_db_path(tmp_path):
    # Use a per-test SQLite DB to isolate queue state
    return str(tmp_path / "mops_test.db")


@pytest.fixture
def base_env(temp_db_path, monkeypatch):
    # Provide a clean environment for each test with isolated DB
    env = os.environ.copy()
    env["MOPS_DB_PATH"] = temp_db_path
    # Keep PATH as-is; we run the binary via explicit path
    return env


def _worker_running(env, mops_bin):
    code, out, _ = run_cmd([mops_bin, "worker", "status"], env=env, check=False)
    return code == 0 and "running" in out.lower()


@pytest.fixture
def worker_session(build_binary, base_env):
    """
    Start a worker if one is not already running, then stop it after the test.
    If a worker is already running (outside of test), skip to avoid interfering
    with user's daemon.
    """
    mops_bin = build_binary
    # If a worker is already running, skip to avoid clobbering external daemon.
    if _worker_running(base_env, mops_bin):
        pytest.skip(
            "mops worker already running on this system; skipping worker-based tests"
        )

    # Start worker
    run_cmd([mops_bin, "worker", "start"], env=base_env)
    assert _worker_running(base_env, mops_bin), "Worker failed to start"

    yield

    # Stop worker (best-effort)
    run_cmd([mops_bin, "worker", "stop"], env=base_env, check=False)


def _list_tasks_json(env, mops_bin, extra_args=None):
    args = [mops_bin, "task", "list", "--json"]
    if extra_args:
        args.extend(extra_args)
    code, out, err = run_cmd(args, env=env, check=True)
    try:
        return json.loads(out)
    except json.JSONDecodeError:
        raise AssertionError(
            f"Failed to parse JSON from task list.\nSTDOUT:\n{out}\nSTDERR:\n{err}"
        )


def _submit_qsub(env, mops_bin, command):
    code, out, _ = run_cmd([mops_bin, "qsub", command], env=env)
    # Output example: "Task 2 submitted to queue: sleep 300"
    m = re.search(r"Task\s+(\d+)\s+submitted", out)
    assert m, f"Could not parse task id from output: {out}"
    return int(m.group(1))


def _wait_for_status(
    env, mops_bin, task_id, wanted_statuses, timeout_s=20, poll_interval=0.2
):
    """
    Poll until task.status is in wanted_statuses. Returns the final task dict or raises on timeout.
    """
    start = time.time()
    wanted = (
        set(wanted_statuses)
        if isinstance(wanted_statuses, (list, set, tuple))
        else {wanted_statuses}
    )
    while time.time() - start < timeout_s:
        tasks = _list_tasks_json(env, mops_bin)
        for t in tasks:
            if t["id"] == task_id:
                if t["status"] in wanted:
                    return t
        time.sleep(poll_interval)
    raise AssertionError(f"Timeout waiting for task {task_id} to be in status {wanted}")


def _read_log(task_id, must_exist=True):
    log_path = Path(f"/tmp/mops_task_{task_id}.log")
    if not log_path.exists():
        if must_exist:
            raise AssertionError(f"Log file not found: {log_path}")
        return ""
    return log_path.read_text()


def test_exec_sync(build_binary, base_env, tmp_path):
    mops_bin = build_binary
    # Simple sync success
    code, out, _ = run_cmd(
        [mops_bin, "task", "exec", "echo SyncOK"], env=base_env, check=False
    )
    assert code == 0
    # Ensure it shows up in list with FINISHED and exit_code=0
    tasks = _list_tasks_json(base_env, mops_bin)
    assert any(
        t["status"] == "FINISHED"
        and (t["exit_code"] in (0, None) or t["exit_code"] == 0)
        for t in tasks
    )

    # Sync failure propagates non-zero
    code, out, _ = run_cmd(
        [mops_bin, "task", "exec", "false"], env=base_env, check=False
    )
    assert code != 0, "Expected non-zero exit for failing command"
    tasks = _list_tasks_json(base_env, mops_bin)
    assert any(t["status"] == "FAILED" for t in tasks)


def test_bg_immediate(build_binary, base_env):
    mops_bin = build_binary
    # Launch background task
    code, out, _ = run_cmd(
        [mops_bin, "task", "bg", "echo 'BG Start' && sleep 1 && echo 'BG End'"],
        env=base_env,
    )
    # Should create a task in RUNNING quickly; allow it to finish
    tasks = _list_tasks_json(base_env, mops_bin)
    assert len(tasks) >= 1
    # Let it complete
    time.sleep(1.5)
    tasks2 = _list_tasks_json(base_env, mops_bin)
    assert any(t["status"] in ("FINISHED", "FAILED") for t in tasks2)


@pytest.mark.timeout(60)
def test_worker_qsub_end_to_end(worker_session, build_binary, base_env):
    mops_bin = build_binary

    # Submit a queued job; worker should pick it up and run
    tid = _submit_qsub(base_env, mops_bin, "echo start && sleep 1 && echo end")
    t = _wait_for_status(
        base_env, mops_bin, tid, wanted_statuses={"FINISHED", "FAILED"}, timeout_s=30
    )
    log = _read_log(tid, must_exist=True)
    assert "start" in log and "end" in log

    # Submit a long job and kill it; expect KILLED if already RUNNING, else CANCELLED
    long_tid = _submit_qsub(base_env, mops_bin, "sleep 30")
    # Wait until it's either RUNNING or remains QUEUED briefly
    try:
        _wait_for_status(
            base_env, mops_bin, long_tid, wanted_statuses={"RUNNING"}, timeout_s=5
        )
    except AssertionError:
        pass  # still QUEUED; kill will cancel it

    run_cmd([mops_bin, "task", "kill", str(long_tid)], env=base_env, check=False)
    t2 = _wait_for_status(
        base_env,
        mops_bin,
        long_tid,
        wanted_statuses={"KILLED", "CANCELLED"},
        timeout_s=15,
    )
    assert t2["status"] in ("KILLED", "CANCELLED")


def test_list_filters_and_pagination(build_binary, base_env):
    mops_bin = build_binary
    # Create a few quick entries
    run_cmd([mops_bin, "task", "exec", "true"], env=base_env, check=False)
    run_cmd([mops_bin, "task", "exec", "false"], env=base_env, check=False)

    # Filter by status
    finished = _list_tasks_json(base_env, mops_bin, ["--status", "FINISHED"])
    failed = _list_tasks_json(base_env, mops_bin, ["--status", "FAILED"])
    assert all(t["status"] == "FINISHED" for t in finished) or len(finished) == 0
    assert all(t["status"] == "FAILED" for t in failed) or len(failed) == 0

    # Order and paginate
    page1 = _list_tasks_json(base_env, mops_bin, ["--order-by", "id", "--limit", "1"])
    page2 = _list_tasks_json(
        base_env, mops_bin, ["--order-by", "id", "--limit", "1", "--offset", "1"]
    )
    if page1 and page2:
        assert page1[0]["id"] != page2[0]["id"]


def test_rm_and_logs(build_binary, base_env):
    mops_bin = build_binary
    # Background task to generate a log
    code, out, _ = run_cmd(
        [mops_bin, "task", "bg", "echo 'X' && sleep 0.3 && echo 'Y'"], env=base_env
    )
    # Find most recent task id
    tasks = _list_tasks_json(base_env, mops_bin)
    assert tasks
    last_id = max(t["id"] for t in tasks)

    # Wait for it to finish and for log to exist
    _wait_for_status(base_env, mops_bin, last_id, {"FINISHED", "FAILED"}, timeout_s=10)
    log_content = _read_log(last_id, must_exist=True)
    assert "X" in log_content or "Y" in log_content

    # Remove record and its log
    run_cmd([mops_bin, "task", "rm", str(last_id), "--log"], env=base_env)
    tasks_after = _list_tasks_json(base_env, mops_bin)
    assert all(t["id"] != last_id for t in tasks_after)
    # Log should be gone (best-effort)
    log_text = _read_log(last_id, must_exist=False)
    assert log_text == ""


@pytest.mark.timeout(60)
def test_purge(build_binary, base_env):
    mops_bin = build_binary
    # Ensure at least two quick tasks exist
    run_cmd([mops_bin, "task", "exec", "true"], env=base_env, check=False)
    run_cmd([mops_bin, "task", "exec", "true"], env=base_env, check=False)

    # Purge tasks older than 1 second
    time.sleep(1.2)
    code, out, _ = run_cmd(
        [mops_bin, "task", "purge", "--older-than", "1s"], env=base_env, check=True
    )
    # Output: "Purged X task(s) ..."
    assert "Purged" in out

    # After purge, recent tasks may be gone; list still works
    tasks = _list_tasks_json(base_env, mops_bin)
    assert isinstance(tasks, list)
