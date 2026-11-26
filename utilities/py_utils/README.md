# py_utils

A [uv](https://docs.astral.sh/uv/) project that holds only Python dependencies, for scripts that build meshes with `classy_blocks` and read or write VTK. It has no package code. The dependencies and the Python version are in `pyproject.toml`, `uv.lock` and `.python-version`.

```bash
cd utilities/py_utils
uv sync                       # create .venv with the locked dependencies
uv run python <script.py>     # run a script with them
```

PGF itself, `pgf` and the tutorials do not need this project.
