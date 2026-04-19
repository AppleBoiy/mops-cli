# Mops Task & Worker System: A Lightweight Job Scheduler

The `mops` task and worker system provides a powerful, persistent, and lightweight job scheduling service, similar in concept to High-Performance Computing (HPC) schedulers like PBS or Slurm. It allows you to submit long-running tasks to a queue, which are then executed sequentially by a background daemon.

This system is ideal for managing batch processing workloads, machine learning training jobs, or any script that needs to run asynchronously without requiring a persistent SSH session.

## The Core Components

There are two primary commands that make up the system:

1.  `mops worker`: Manages the lifecycle of the persistent scheduler daemon.
2.  `mops task`: Submits and interacts with individual jobs in the queue.

---

## 1. The Worker Daemon (`mops worker`)

The worker is the heart of the scheduler. It's a single, detached background process that continuously polls the database for new jobs to run. You typically only need to start it once per server reboot.

### `mops worker start`

Starts the scheduler daemon. The process is forked into the background, detached from your terminal, and its Process ID (PID) is stored in `/tmp/mops_worker.pid`.

```bash
$ mops worker start
Starting mops worker daemon...
```

### `mops worker status`

Checks if the worker daemon is currently running by reading the PID file and verifying the process exists.

```bash
$ mops worker status
mops worker: running (PID 12345)
```

### `mops worker stop`

Gracefully shuts down the worker daemon by sending it a `SIGTERM` signal. The daemon will finish its current task before exiting.

```bash
$ mops worker stop
Sent SIGTERM to mops worker (PID 12345).
Worker has shut down.
```

---

## 2. Managing Jobs (`mops task`)

The `task` subcommand is your interface for submitting and managing jobs.

### Job Lifecycle

A job submitted to `mops` goes through a simple state machine:

1.  **QUEUED**: The job has been successfully added to the database and is waiting to be executed.
2.  **RUNNING**: The worker has picked up the job and is currently executing it.
3.  **FINISHED**: The job completed successfully (exit code 0).
4.  **FAILED**: The job terminated with a non-zero exit code.
5.  **KILLED**: The job was manually stopped with `mops task kill`.
6.  **CANCELLED**: The job was cancelled while still in the queue (before it started).

### Commands & Aliases

To provide a familiar experience for those used to HPC environments, `mops` includes standard PBS-style aliases for its most common task commands.

| Primary Command        | PBS Alias | Description                                        |
| ---------------------- | --------- | -------------------------------------------------- |
| `mops task submit ...` | `qsub`    | Submits a new job to the queue.                    |
| `mops task list`       | `qstat`   | Lists all jobs and their current status.           |
| `mops task kill <id>`  | `qdel`    | Kills a running job.                               |

#### `submit` / `qsub`

Adds a new job to the queue. `mops` will print the Task ID assigned to the job.

```bash
$ mops task submit "python train_model.py --epochs 50"
Task 1 submitted to queue: python train_model.py --epochs 50

$ mops qsub "sleep 300"
Task 2 submitted to queue: sleep 300
```

#### `list` / `qstat`

Displays the status of all jobs tracked by the system.

```bash
$ mops task list
ID    | PID        | STATUS     | COMMAND
--------------------------------------------------------------
1     | 23456      | RUNNING    | python train_model.py --epochs 50
2     | 0          | QUEUED     | sleep 300
```

#### `kill` / `qdel`

Stops a running job. If the job is still in the QUEUED state, it will be cancelled (status set to CANCELLED) without sending a signal.

```bash
$ mops task kill 1
Sent SIGTERM to task 1 (PID 23456)
```

```bash
$ mops task kill 2
Cancelled queued task 2
```

#### `logs`

View the output (`stdout` and `stderr`) of any job that has been run by the worker. Use the `--tail` flag to follow the log in real-time.

```bash
# View the entire log for task 1
mops task logs 1

# Watch the live output of task 1
mops task logs 1 --tail
```

#### `clean`

A utility to scan the system for zombie (`Z`) processes or orphaned python workers, with an option to aggressively terminate them.

```bash
# Scan for zombies and orphans
mops task clean

# Scan and forcefully kill any found orphans
mops task clean --force
```

---

## A Complete Workflow Example

1.  **Start the daemon** on your server.
    ```bash
    mops worker start
    ```

2.  **Submit a few jobs.**
    ```bash
    mops qsub "echo 'Starting data processing...' && sleep 20 && echo 'Done.'"
    mops qsub "python run_simulations.py"
    ```

3.  **Check the queue status.**
    ```bash
    mops qstat
    # Shows job 1 RUNNING, job 2 QUEUED
    ```

4.  **Tail the logs** of the running job to monitor its progress.
    ```bash
    mops task logs 1 --tail
    ```

5.  Once all jobs are finished, you can **stop the worker** if desired.
    ```bash
    mops worker stop
    ```

## System Files

The `mops` scheduler uses the following files:

-   `mops.db`: An SQLite database file created in the directory where you run `mops`. It stores the state of all jobs.
-   `/tmp/mops_worker.pid`: A file containing the Process ID of the running worker daemon.
-   `/tmp/mops_task_<id>.log`: The file where `stdout` and `stderr` for each executed job are redirected.

---

## Running the Test Suite

The project includes a pytest-based integration test suite that exercises end-to-end flows (exec, bg, qsub/worker, kill, logs, rm, purge, list filters).

Prerequisites:
- Linux recommended (some sys/clean features rely on /proc; tests skip worker-based flows if a worker is already running)
- Python 3.8+
- make, gcc/clang
- curl (for webhook tests if enabled)

Setup:
```bash
# Optional: create and activate a virtual environment
python3 -m venv .venv
. .venv/bin/activate

# Install test dependencies
pip install pytest
```

Build the binary:
```bash
make clean && make -j4
```

Run all tests:
```bash
pytest -q tests
```

Run a single test:
```bash
pytest -q tests/test_task_flow.py::test_worker_qsub_end_to_end
```

Notes:
- Tests use an isolated SQLite DB per test via MOPS_DB_PATH, so they won’t touch your existing mops.db.
- Worker tests will skip if a system worker is already running to avoid interfering with an external daemon.
- You can still run CLI commands manually while using the same DB by exporting MOPS_DB_PATH to the same path the tests use.