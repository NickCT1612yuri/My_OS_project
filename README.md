# My_OS_project

Simple Operating System simulation project for scheduling, syscall handling, and paging-based memory management.

## Prerequisites

- macOS/Linux shell
- `gcc`
- `make`
- `pthread` (usually available by default)
- Python 3 (for report image generation)
- LaTeX (`pdflatex`) for report build

## Project Structure

- `src/`: OS simulator source code
- `include/`: headers
- `input/`: simulation configs and process programs
- `output/`, `output_actual/`: expected and actual outputs
- `run.sh`: helper script to generate multiple actual outputs
- `Report/`: report sources, sections, and images

## Build The OS Simulator

From repository root:

```bash
make all
```

This produces the executable:

- `./os`

To clean build artifacts:

```bash
make clean
```

## Run Simulations

Run one scenario:

```bash
./os <config_name>
```

Example:

```bash
./os sched_0
```

Run the prepared batch script:

```bash
chmod +x run.sh
./run.sh
```

This writes outputs to `output_actual/`.

## Generate And Build The Report

From repository root:

```bash
cd Report
python3 generate_images.py
pdflatex -interaction=nonstopmode -halt-on-error main.tex
pdflatex -interaction=nonstopmode -halt-on-error main.tex
```

Output report:

- `Report/main.pdf`

Notes:

- Running `pdflatex` twice is recommended so references/page numbers stabilize.
- If you update figures or section content, run `python3 generate_images.py` before LaTeX.

## Quick End-To-End

```bash
make all
./os sched_0
cd Report
python3 generate_images.py
pdflatex -interaction=nonstopmode -halt-on-error main.tex
pdflatex -interaction=nonstopmode -halt-on-error main.tex
```
