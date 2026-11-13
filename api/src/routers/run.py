'''
/api/run

Accepts source code, language, and custom stdin.

Compiles the code into a temporary directory.

Invokes the sandbox binary to run it.

Returns the stdout, stderr, and exit code.
'''

import asyncio
import os
import shutil
import tempfile
import uuid
from pathlib import Path

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field

from ..deps import CurrentUser, get_current_user

router = APIRouter(prefix="/api", tags=["run"])

class RunRequest(BaseModel):
    language: str = Field(..., pattern="^(c|cpp|python)$")
    source_code: str = Field(..., max_length=65536)
    stdin: str = Field("", max_length=4096)

class RunResponse(BaseModel):
    stdout: str
    stderr: str
    exit_code: int
    timed_out: bool
    runtime_ms: int

# Path to the sandbox binary (built by `make -C os/sandbox`)
SANDBOX_BIN = Path(__file__).resolve().parent.parent.parent.parent / "os" / "sandbox" / "sandbox"

@router.post("/run", response_model=RunResponse)
async def run_custom_code(
    req: RunRequest,
    user: CurrentUser = Depends(get_current_user),
):
    """
    Compile and run a snippet of code with custom stdin.
    Returns the output immediately. Bypasses the grading queue.
    """
    if not SANDBOX_BIN.exists():
        raise HTTPException(status_code=500, detail="Sandbox binary not found. Run 'make -C os/sandbox'")

    # Create a unique temporary directory for this run
    tmpdir = Path(tempfile.gettempdir()) / f"codearena_run_{uuid.uuid4().hex}"
    tmpdir.mkdir(parents=True, exist_ok=True)

    try:
        src_path = tmpdir / "sol.c" if req.language == "c" else (tmpdir / "sol.cpp" if req.language == "cpp" else tmpdir / "sol.py")
        bin_path = tmpdir / "sol"
        
        src_path.write_text(req.source_code)

        # 1. Compile (if necessary)
        if req.language in ("c", "cpp"):
            compiler = "gcc" if req.language == "c" else "g++"
            std_flag = "-std=c11" if req.language == "c" else "-std=c++17"
            
            proc = await asyncio.create_subprocess_exec(
                compiler, "-O2", std_flag, "-o", str(bin_path), str(src_path),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            _, stderr = await proc.communicate()
            
            if proc.returncode != 0:
                return RunResponse(
                    stdout="",
                    stderr=f"Compilation Error:\n{stderr.decode(errors='replace')}",
                    exit_code=proc.returncode,
                    timed_out=False,
                    runtime_ms=0,
                )
        else:
            # Python doesn't compile
            bin_path = src_path

        # 2. Run through the sandbox
        # We set a strict 5-second timeout for custom runs.
        args = [
            str(SANDBOX_BIN),
            "--binary", str(bin_path),
            "--input", req.stdin,
            "--time", "5000",
            "--grace-ms", "500",
            "--cgroup-memory", "128",
            "--cpu-seconds", "2",
            "--output-limit", "8192",   # 8KB limit for custom runs
            # We do not use --all-ns here to keep it fast and less prone
            # to permission issues on the host machine. The sandbox still
            # uses rlimits, seccomp, and cgroups.
        ]

        proc = await asyncio.create_subprocess_exec(
            *args,
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        
        # Feed the custom stdin
        stdout_bytes, stderr_bytes = await proc.communicate()

        # The sandbox prints a JSON-like report to stdout, we need to parse it.
        # However, the sandbox currently dumps the program's stdout between
        # markers. Let's use a simpler approach: capture everything and
        # extract the STDOUT block.
        output = stdout_bytes.decode(errors="replace")
        error_output = stderr_bytes.decode(errors="replace")
        
        # The sandbox output format is:
        # === SANDBOX RESULT ===
        # verdict: ...
        # ...
        # --- STDOUT ---
        # <program output>
        # --- STDERR ---
        # <program stderr>
        
        prog_stdout = ""
        prog_stderr = ""
        timed_out = False
        exit_code = proc.returncode or 0

        if "--- STDOUT ---" in output:
            parts = output.split("--- STDOUT ---")
            if len(parts) > 1:
                remainder = parts[1]
                if "--- STDERR ---" in remainder:
                    prog_stdout, prog_stderr = remainder.split("--- STDERR ---")
                else:
                    prog_stdout = remainder
        
        # Clean up separators
        prog_stdout = prog_stdout.split("=== END ===")[0].strip()
        prog_stderr = prog_stderr.split("=== END ===")[0].strip()
        
        # Override prog_stderr if the sandbox itself produced errors
        if "sandbox_run failed" in error_output or not prog_stdout:
            if "stderr:" in output:
                prog_stderr = output.split("stderr:")[1].split("\n")[0].strip()

        timed_out = "verdict:            TLE" in output or "timed_out:          1" in output
        
        # Extract runtime from sandbox output
        runtime_ms = 0
        if "wall_time_ms:" in output:
            try:
                runtime_ms = int(output.split("wall_time_ms:")[1].split("\n")[0].strip())
            except ValueError:
                pass

        return RunResponse(
            stdout=prog_stdout,
            stderr=prog_stderr,
            exit_code=exit_code,
            timed_out=timed_out,
            runtime_ms=runtime_ms,
        )

    finally:
        # Always clean up the temporary directory
        shutil.rmtree(tmpdir, ignore_errors=True)