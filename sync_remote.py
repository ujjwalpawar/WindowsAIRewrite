import os
import subprocess
import sys

ROOT = r"C:\Users\ujjwa\WindowsAIRewrite"
GIT = r"C:\Program Files\Git\cmd\git.exe"
GH = r"C:\Program Files\GitHub CLI\gh.exe"
REMOTE = "https://github.com/ujjwalpawar/WindowsAIRewrite.git"

env = os.environ.copy()
env.update(
    {
        "GIT_MASTER": "1",
        "GIT_TERMINAL_PROMPT": "0",
        "GIT_EDITOR": ":",
        "GIT_SEQUENCE_EDITOR": ":",
        "GIT_MERGE_AUTOEDIT": "no",
        "GIT_PAGER": "cat",
        "PAGER": "cat",
        "GIT_AUTHOR_NAME": "ujjwalpawar",
        "GIT_AUTHOR_EMAIL": "ujjwalpawar@users.noreply.github.com",
        "GIT_COMMITTER_NAME": "ujjwalpawar",
        "GIT_COMMITTER_EMAIL": "ujjwalpawar@users.noreply.github.com",
    }
)
env["PATH"] = r"C:\Program Files\Git\cmd;" + env.get("PATH", "")


def run(args, check=True):
    print("$ " + " ".join(args))
    result = subprocess.run(args, cwd=ROOT, env=env, text=True, capture_output=True)
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    if check and result.returncode != 0:
        raise SystemExit(result.returncode)
    return result


run([GH, "auth", "setup-git"])

status = run([GIT, "status", "--short"]).stdout.strip()
if status:
    run([GIT, "add", "."])
    run(
        [
            GIT,
            "commit",
            "-m",
            "feat: refine model settings and rewrite UX",
            "-m",
            "Ultraworked with [Sisyphus](https://github.com/code-yeongyu/oh-my-openagent)",
            "-m",
            "Co-authored-by: Sisyphus <clio-agent@sisyphuslabs.ai>",
        ]
    )

remotes = run([GIT, "remote"]).stdout.split()
if "origin" not in remotes:
    run([GIT, "remote", "add", "origin", REMOTE])
else:
    run([GIT, "remote", "set-url", "origin", REMOTE])

run([GIT, "push", "-u", "origin", "main"])
run([GIT, "status", "--short"])
run([GIT, "log", "--oneline", "-6"])
print("PUSHED_URL=https://github.com/ujjwalpawar/WindowsAIRewrite")
