"""
generate_images.py
Generates all figures for the Simple OS Lab Report (main.tex).
Each function produces one image in the Images/ directory.
"""
import os
import re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.patheffects as pe
import numpy as np
from PIL import Image, ImageDraw, ImageFont

OUT = "Images"
os.makedirs(OUT, exist_ok=True)


def sanitize_png(path):
    """Normalize PNG outputs for LaTeX/report embedding.

    - Flatten alpha to white background
    - Save as clean RGB PNG
    """
    img = Image.open(path)
    if img.mode in ("RGBA", "LA"):
        bg = Image.new("RGB", img.size, "white")
        alpha = img.split()[-1]
        bg.paste(img, mask=alpha)
        img = bg
    elif img.mode != "RGB":
        img = img.convert("RGB")
    img.save(path, format="PNG", optimize=True)

# ─── Color Palette ────────────────────────────────────────────────────────────
PALETTE = ['#E74C3C','#3498DB','#2ECC71','#F39C12','#9B59B6',
           '#1ABC9C','#E67E22','#34495E','#E91E63','#00BCD4']
BG      = '#1E1E2E'
GREEN   = '#00FF7F'
TEAL    = '#00CFFF'
ORANGE  = '#FFB347'

# ══════════════════════════════════════════════════════════════════════════════
# 1. Gantt Charts – parse actual output files
# ══════════════════════════════════════════════════════════════════════════════
def parse_output(filepath, num_cpus):
    """Parse a scheduler actual.output file into Gantt segments."""
    tasks = []
    current_time = 0
    running = {}          # cpu_id -> (pid, prio, start_time)
    prio_map = {}         # pid -> prio (from "Loaded" lines)

    with open(filepath, 'r') as f:
        for line in f:
            line = line.rstrip()
            m_slot = re.search(r'^Time slot\s+(\d+)', line)
            if m_slot:
                current_time = int(m_slot.group(1))
                continue

            m_load = re.search(r'PID:\s*(\d+)\s+PRIO:\s*(\d+)', line)
            if m_load:
                prio_map[int(m_load.group(1))] = int(m_load.group(2))

            m_disp = re.search(r'CPU\s+(\d+):\s+Dispatched process\s+(\d+)', line)
            if m_disp:
                c, p = int(m_disp.group(1)), int(m_disp.group(2))
                running[c] = (p, prio_map.get(p, 0), current_time)

            if re.search(r'CPU\s+\d+:\s+(Put process|Processed .* has finished)', line):
                m_cpu = re.search(r'CPU\s+(\d+)', line)
                if m_cpu:
                    c = int(m_cpu.group(1))
                    if c in running:
                        pid, prio, st = running[c]
                        dur = current_time - st
                        if dur > 0:
                            tasks.append((st, dur, c, pid, prio))
                        del running[c]

    # close any still-running
    for c, (pid, prio, st) in running.items():
        dur = current_time - st
        if dur > 0:
            tasks.append((st, dur, c, pid, prio))

    return tasks


def draw_gantt(out_path, title, tasks, num_cpus, subtitle=""):
    if not tasks:
        fig, ax = plt.subplots(figsize=(10, 3))
        ax.text(0.5, 0.5, "No dispatch events parsed", ha='center', va='center',
                transform=ax.transAxes, fontsize=12, color='gray')
        ax.set_title(title)
        plt.tight_layout()
        plt.savefig(out_path, dpi=200, bbox_inches='tight', facecolor='white')
        plt.close()
        sanitize_png(out_path)
        return

    all_pids = sorted(set(t[3] for t in tasks))
    pid_colors = {pid: PALETTE[i % len(PALETTE)] for i, pid in enumerate(all_pids)}

    max_time = max(t[0] + t[1] for t in tasks) + 2
    fig_w = max(12, max_time * 0.35)
    fig_h = max(3, num_cpus * 1.6 + 1.5)
    fig, ax = plt.subplots(figsize=(fig_w, fig_h))
    fig.patch.set_facecolor('#F8F9FA')
    ax.set_facecolor('#FFFFFF')

    # Grid
    for x in range(0, int(max_time)+1, 2):
        ax.axvline(x, color='#DEE2E6', lw=0.5, zorder=0)

    # CPU rows
    for cpu_id in range(num_cpus):
        y = num_cpus - cpu_id
        ax.axhline(y, color='#CED4DA', lw=0.5, zorder=0)
        ax.text(-0.3, y, f'CPU {cpu_id}', ha='right', va='center',
                fontsize=9, fontweight='bold', color='#343A40')

    # Bars
    for st, dur, cpu, pid, prio in tasks:
        y = num_cpus - cpu
        color = pid_colors[pid]
        bar = ax.barh(y, dur, left=st, height=0.65, align='center',
                      color=color, edgecolor='white', linewidth=0.8,
                      alpha=0.92, zorder=2)
        if dur >= 1.2:
            ax.text(st + dur/2, y, f'P{pid}\n(PR:{prio})',
                    ha='center', va='center', fontsize=7.5,
                    fontweight='bold', color='white',
                    path_effects=[pe.withStroke(linewidth=1, foreground='black')],
                    zorder=3)

    # Legend
    legend_patches = [mpatches.Patch(color=pid_colors[p],
                                      label=f'PID {p} (PRIO {tasks[[t[3] for t in tasks].index(p)][4]})')
                      for p in all_pids]
    ax.legend(handles=legend_patches, loc='upper right', fontsize=8,
              framealpha=0.9, ncol=min(5, len(all_pids)))

    ax.set_xlim(-1, max_time)
    ax.set_ylim(0.3, num_cpus + 0.7)
    ax.set_yticks([])
    ax.set_xlabel('Time Slot', fontsize=10)
    ax.set_title(title + ('\n' + subtitle if subtitle else ''), fontsize=11,
                 fontweight='bold', pad=8)

    # Time labels
    for x in range(0, int(max_time)+1, max(1, int(max_time)//20)):
        ax.text(x, 0.1, str(x), ha='center', va='bottom', fontsize=7,
                color='#495057', transform=ax.get_xaxis_transform())

    plt.tight_layout()
    plt.savefig(out_path, dpi=200, bbox_inches='tight', facecolor='white')
    plt.close()
    sanitize_png(out_path)
    print(f"Generated {out_path}")


def generate_gantts():
    configs = [
        ('output_actual/sched.actual.output',   2, 'sched.actual.output — 2 CPUs',
         'MLQ Scheduling: processes dispatched across 2 parallel CPUs'),
        ('output_actual/sched_0.actual.output', 1, 'sched_0.actual.output — 1 CPU',
         'Single-CPU MLQ: intra-queue Round Robin + priority preemption'),
        ('output_actual/sched_1.actual.output', 1, 'sched_1.actual.output — 1 CPU',
         'Single-CPU MLQ: alternative process set'),
    ]
    fnames = ['gantt_sched.png', 'gantt_sched_0.png', 'gantt_sched_1.png']
    for (path, ncpus, title, sub), fname in zip(configs, fnames):
        tasks = parse_output(path, ncpus)
        draw_gantt(f'{OUT}/{fname}', title, tasks, ncpus, subtitle=sub)


def _extract_time_window_lines(filepath, start_slot, end_slot=None, max_lines=24):
    """Extract raw log lines for a time-slot window from an actual output file."""
    with open(filepath, 'r') as f:
        raw = [ln.rstrip('\n') for ln in f]

    slot_re = re.compile(r'^Time slot\s+(\d+)')
    start_idx = None
    for i, ln in enumerate(raw):
        m = slot_re.search(ln.strip())
        if m and int(m.group(1)) == start_slot:
            start_idx = i
            break

    if start_idx is None:
        return []

    out = []
    for ln in raw[start_idx:]:
        m = slot_re.search(ln.strip())
        if m and end_slot is not None and int(m.group(1)) > end_slot:
            break
        if ln.strip() == '':
            continue
        out.append(ln)
        if len(out) >= max_lines:
            break
    return out


def _extract_context_lines(filepath, token, before=1, after=1):
    """Extract a small context window around the first line containing token."""
    with open(filepath, 'r') as f:
        raw = [ln.rstrip('\n') for ln in f]

    for i, ln in enumerate(raw):
        if token in ln:
            lo = max(0, i - before)
            hi = min(len(raw), i + after + 1)
            return [x for x in raw[lo:hi] if x.strip()]
    return []


# ══════════════════════════════════════════════════════════════════════════════
# 2. terminal_output.png  — print_pgtbl output from os_1_mlq_paging
# ══════════════════════════════════════════════════════════════════════════════
def generate_terminal_pic():
    W, H = 1320, 420
    img = Image.new('RGB', (W, H), color='#1E1E2E')
    draw = ImageDraw.Draw(img)

    # Terminal chrome
    draw.rectangle([(0,0),(W,36)], fill='#2D2D3F')
    draw.text((W//2, 18), '  os_1_mlq_paging.actual.output  -  Time Slot 14',
              fill='#CDD6F4', anchor='mm')
    for cx, col in [(18,'#FF5F56'),(43,'#FFBD2E'),(68,'#27C93F')]:
        draw.ellipse([(cx-9,10),(cx+9,26)], fill=col)

    try:
        mono = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 18)
        sans = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 15)
    except:
        mono = sans = ImageFont.load_default()

    lines = _extract_time_window_lines('output_actual/os_1_mlq_paging.actual.output', 14, 15, max_lines=16)
    if not lines:
        lines = ['[parse error] Could not extract Time slot 14-15 from output_actual/os_1_mlq_paging.actual.output']

    y = 56
    for text in lines:
        t = text.strip()
        fg = '#CDD6F4'
        if 'Loaded' in t:
            fg = '#A6E3A1'
        elif 'Dispatched process' in t or 'Put process' in t:
            fg = '#89DCEB'
        elif 'liballoc' in t:
            fg = '#A6E3A1'
        elif 'PDG=' in t or 'P4g=' in t or 'PUD=' in t or 'PMD=' in t:
            fg = '#F9E2AF'
        draw.text((16, y), text, fill=fg, font=mono)
        y += 31

    img.save(f'{OUT}/terminal_output.png')
    sanitize_png(f'{OUT}/terminal_output.png')
    print(f"Generated {OUT}/terminal_output.png")


# ══════════════════════════════════════════════════════════════════════════════
# 3. terminal_output_sec82.png  — annotated os_2_mlq_paging trace
# ══════════════════════════════════════════════════════════════════════════════
def generate_terminal_sec82():
    W, H = 1320, 470
    img = Image.new('RGB', (W, H), color='#1E1E2E')
    draw = ImageDraw.Draw(img)

    draw.rectangle([(0,0),(W,36)], fill='#2D2D3F')
    draw.text((W//2, 18), '  os_2_mlq_paging.actual.output  -  kmalloc privilege isolation',
              fill='#CDD6F4', anchor='mm')
    for cx, col in [(18,'#FF5F56'),(43,'#FFBD2E'),(68,'#27C93F')]:
        draw.ellipse([(cx-9,10),(cx+9,26)], fill=col)

    try:
        mono = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 18)
        sans = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 14)
    except:
        mono = sans = ImageFont.load_default()

    lines = _extract_time_window_lines('output_actual/os_2_mlq_paging.actual.output', 5, 10, max_lines=24)
    fail_ctx = _extract_context_lines('output_actual/os_2_mlq_paging.actual.output', 'libfree:218 failed', before=1, after=0)
    if fail_ctx:
        lines.extend(['Time slot  15+ (later)'] + fail_ctx)

    if not lines:
        lines = ['[parse error] Could not extract data from output_actual/os_2_mlq_paging.actual.output']

    y = 56
    block_top, block_bot = 0, 0
    for text in lines:
        t = text.strip()
        fg = '#CDD6F4'
        if 'Loaded' in t:
            fg = '#A6E3A1'
        elif 'Dispatched process' in t or 'Put/Dispatched' in t:
            fg = '#89DCEB'
        elif 'liballoc' in t:
            fg = '#A6E3A1'
        elif 'libfree:218 failed' in t:
            fg = '#FF5555'
        elif 'libfree:218' in t:
            fg = TEAL
        elif '(no output)' in t:
            fg = '#6C7086'

        draw.text((16, y), text, fill=fg, font=mono)
        if '(no output)' in t:
            block_top = y - 6
            block_bot = y + 28
        y += 27

    if block_top and block_bot:
        draw.rectangle([(12, block_top), (W - 12, block_bot)], outline='#F9E2AF', width=2)
        draw.text((18, block_top - 24), 'Silent rejection window', fill='#F9E2AF', font=sans)

    img.save(f'{OUT}/terminal_output_sec82.png')
    sanitize_png(f'{OUT}/terminal_output_sec82.png')
    print(f"Generated {OUT}/terminal_output_sec82.png")


# ══════════════════════════════════════════════════════════════════════════════
# 4. q1_viz.png  — MLQ slot budget depletion diagram (matches actual sched_0)
# ══════════════════════════════════════════════════════════════════════════════
def generate_q1_viz():
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.2), facecolor='#F8F9FA')
    fig.suptitle('Unified System Call Interface: Trade-off Overview',
                 fontsize=13, fontweight='bold')

    ax = axes[0]
    ax.set_facecolor('#FFFFFF')
    pros = ['Portability', 'Unified security', 'Design simplicity', 'Composability']
    pvals = [9, 8, 7, 9]
    y = np.arange(len(pros))
    ax.barh(y, pvals, color='#2ECC71', alpha=0.9)
    ax.set_yticks(y)
    ax.set_yticklabels(pros, fontsize=9)
    ax.set_xlim(0, 10)
    ax.set_title('Advantages (higher is better)', fontsize=10)
    ax.grid(axis='x', linestyle='--', alpha=0.3)

    ax2 = axes[1]
    ax2.set_facecolor('#FFFFFF')
    cons = ['VFS overhead', 'Abstraction leaks', 'Error complexity', 'ioctl portability risk']
    cvals = [6, 8, 7, 7]
    y2 = np.arange(len(cons))
    ax2.barh(y2, cvals, color='#E74C3C', alpha=0.9)
    ax2.set_yticks(y2)
    ax2.set_yticklabels(cons, fontsize=9)
    ax2.set_xlim(0, 10)
    ax2.set_title('Disadvantages (higher is worse)', fontsize=10)
    ax2.grid(axis='x', linestyle='--', alpha=0.3)

    plt.tight_layout(rect=[0, 0, 1, 0.93])
    plt.savefig(f'{OUT}/q1_viz.png', dpi=200, bbox_inches='tight', facecolor='white')
    plt.close()
    sanitize_png(f'{OUT}/q1_viz.png')
    print(f"Generated {OUT}/q1_viz.png")


def generate_q3_viz():
    tasks = sorted(parse_output('output_actual/sched_0.actual.output', 1), key=lambda t: (t[0], t[3]))
    fig, axes = plt.subplots(2, 1, figsize=(13, 6.8), facecolor='#F8F9FA')
    fig.suptitle('MLQ Policies in Action (from sched_0.actual.output)', fontsize=13, fontweight='bold')

    ax = axes[0]
    ax.set_facecolor('#FFFFFF')
    if not tasks:
        ax.axis('off')
        ax.text(0.5, 0.5, 'No parsed scheduler events', ha='center', va='center', transform=ax.transAxes)
    else:
        prio_to_pids = {}
        for _, _, _, pid, prio in tasks:
            prio_to_pids.setdefault(prio, set()).add(pid)
        prios = sorted(prio_to_pids.keys())
        budgets = [max(1, 140 - p) for p in prios]
        ax.bar([str(p) for p in prios], budgets, color=['#E74C3C' if p == min(prios) else '#3498DB' for p in prios])
        ax.set_title('Policy A: Priority Queue Slot Budgets', fontsize=10)
        ax.set_xlabel('Priority level')
        ax.set_ylabel('slot = 140 - prio')
        ax.grid(axis='y', linestyle='--', alpha=0.3)

    ax2 = axes[1]
    ax2.set_facecolor('#FFFFFF')
    if not tasks:
        ax2.axis('off')
    else:
        pid_prio = {}
        for st, dur, _, pid, prio in tasks:
            pid_prio[pid] = prio
            color = '#E74C3C' if prio == min(pid_prio.values()) else '#F39C12' if prio <= 4 else '#3498DB'
            ax2.barh(0, dur, left=st, height=0.5, color=color, edgecolor='white')
            if dur >= 1:
                ax2.text(st + dur/2, 0, f'P{pid}', ha='center', va='center', fontsize=8, color='white', fontweight='bold')
        ax2.set_yticks([])
        ax2.set_xlabel('Time slot')
        ax2.set_title('Policy B/C: Round Robin + Priority Preemption Timeline', fontsize=10)
        ax2.grid(axis='x', linestyle='--', alpha=0.3)

    plt.tight_layout(rect=[0, 0, 1, 0.93])
    plt.savefig(f'{OUT}/q3_viz.png', dpi=200, bbox_inches='tight', facecolor='white')
    plt.close()
    sanitize_png(f'{OUT}/q3_viz.png')
    print(f"Generated {OUT}/q3_viz.png")


# ══════════════════════════════════════════════════════════════════════════════
# 5. q2_watchdog.png  — Syscall timing diagram
# ══════════════════════════════════════════════════════════════════════════════
def generate_watchdog_pic():
    fig, ax = plt.subplots(figsize=(12, 4.6), facecolor='#F8F9FA')
    ax.set_facecolor('#FFFFFF')
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 10)
    ax.axis('off')
    ax.set_title('Long Syscall Detection and Handling Timeline', fontsize=12, fontweight='bold', pad=10)

    # Normal path
    y1 = 7.2
    ax.annotate('', xy=(96, y1), xytext=(4, y1), arrowprops=dict(arrowstyle='->', lw=1.8, color='#34495E'))
    ax.add_patch(mpatches.FancyBboxPatch((8, y1-0.45), 25, 0.9, boxstyle='round,pad=0.03',
                                         facecolor='#85C1E9', edgecolor='#2980B9', linewidth=1.5))
    ax.text(20.5, y1, 'Normal syscall executes and returns', ha='center', va='center', fontsize=9, fontweight='bold')
    ax.axvline(8, ymin=0.62, ymax=0.76, color='#27AE60', lw=2)
    ax.text(8, y1-1.05, 'Timer armed', ha='center', fontsize=8, color='#27AE60')
    ax.axvline(33, ymin=0.62, ymax=0.76, color='#2980B9', lw=2)
    ax.text(33, y1-1.05, 'Syscall return', ha='center', fontsize=8, color='#2980B9')
    ax.axvline(54, ymin=0.62, ymax=0.76, color='#E67E22', lw=2, ls='--')
    ax.text(54, y1-1.05, 'Watchdog limit', ha='center', fontsize=8, color='#E67E22')

    # Runaway path
    y2 = 4.1
    ax.annotate('', xy=(96, y2), xytext=(4, y2), arrowprops=dict(arrowstyle='->', lw=1.8, color='#922B21'))
    ax.add_patch(mpatches.FancyBboxPatch((8, y2-0.45), 50, 0.9, boxstyle='round,pad=0.03',
                                         facecolor='#F1948A', edgecolor='#C0392B', linewidth=1.5))
    ax.text(33, y2, 'Runaway syscall (blocked/spinning)', ha='center', va='center', fontsize=9, fontweight='bold', color='#7B241C')
    ax.axvline(8, ymin=0.31, ymax=0.45, color='#27AE60', lw=2)
    ax.axvline(58, ymin=0.31, ymax=0.53, color='#E74C3C', lw=3)
    ax.text(58, y2-1.25, 'Timer interrupt preempts and scheduler regains CPU', ha='center', fontsize=8, color='#E74C3C', fontweight='bold')

    ax.add_patch(mpatches.FancyBboxPatch((5, 0.7), 90, 1.5, boxstyle='round,pad=0.08',
                                         facecolor='#EBF5FB', edgecolor='#2980B9', linewidth=1.2))
    ax.text(50, 1.45, 'Simple OS mapping: each time_slot executes one step then scheduler re-evaluates get_proc()/put_proc()',
            ha='center', va='center', fontsize=8.5, color='#1A5276')

    plt.tight_layout()
    plt.savefig(f'{OUT}/q2_watchdog.png', dpi=200, bbox_inches='tight', facecolor='white')
    plt.close()
    sanitize_png(f'{OUT}/q2_watchdog.png')
    print(f"Generated {OUT}/q2_watchdog.png")


# ══════════════════════════════════════════════════════════════════════════════
# 6. q4_segmentation_paging.png — Segmentation + Paging hybrid
# ══════════════════════════════════════════════════════════════════════════════
def generate_seg_paging_pic():
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.8), facecolor='#F8F9FA')
    fig.suptitle('Why Segmentation + Paging Hybrid Works', fontsize=13, fontweight='bold')

    ax = axes[0]
    ax.set_facecolor('#FFFFFF')
    ax.set_title('Segmentation only')
    ax.barh([3, 2, 1], [6, 4, 5], color=['#AED6F1', '#A9DFBF', '#F9E79F'])
    ax.barh([2.5, 1.5], [1.3, 0.9], color='#FADBD8', alpha=0.9)
    ax.text(0.1, 2.5, 'holes', color='#922B21', fontsize=8, va='center')
    ax.text(0.1, 1.5, 'holes', color='#922B21', fontsize=8, va='center')
    ax.set_xlim(0, 7)
    ax.set_yticks([])
    ax.set_xlabel('physical space')

    ax2 = axes[1]
    ax2.set_facecolor('#FFFFFF')
    ax2.set_title('Paging only')
    vals = [1, 1, 1, 1, 1, 1, 1]
    cols = ['#85C1E9', '#F9E79F', '#A9DFBF', '#D7BDE2', '#FDEBD0', '#85C1E9', '#A9DFBF']
    ax2.bar(range(len(vals)), vals, color=cols)
    ax2.text(0.1, 1.05, 'No external fragmentation, but no logical boundaries', fontsize=8, color='#2C3E50')
    ax2.set_ylim(0, 1.3)
    ax2.set_yticks([])
    ax2.set_xticks([])

    ax3 = axes[2]
    ax3.set_facecolor('#FFFFFF')
    ax3.set_title('Hybrid approach')
    benefits = ['Logical segments', 'Page-level mapping', 'Low fragmentation', 'Protection domains']
    bvals = [9, 9, 8, 9]
    ax3.barh(np.arange(len(benefits)), bvals, color='#2ECC71')
    ax3.set_yticks(np.arange(len(benefits)))
    ax3.set_yticklabels(benefits, fontsize=8)
    ax3.set_xlim(0, 10)
    ax3.grid(axis='x', linestyle='--', alpha=0.3)

    plt.tight_layout(rect=[0, 0, 1, 0.93])
    plt.savefig(f'{OUT}/q4_segmentation_paging.png', dpi=200, bbox_inches='tight', facecolor='white')
    plt.close()
    sanitize_png(f'{OUT}/q4_segmentation_paging.png')
    print(f"Generated {OUT}/q4_segmentation_paging.png")


# ══════════════════════════════════════════════════════════════════════════════
# 7. q5_viz.png  — Race condition on free_fp_list (accurate from code)
# ══════════════════════════════════════════════════════════════════════════════
def generate_q5_viz():
    # 64-bit VA with 4KB pages, 8-byte PTE.
    flat_bytes = (2**52) * 8
    flat_pb = flat_bytes / (1024**5)

    # Example sparse working set to illustrate hierarchical savings.
    active_pages = 4096
    pt_pages = int(np.ceil(active_pages / 512))
    upper_pages = 3
    sparse_bytes = (pt_pages + upper_pages) * 4096
    sparse_kb = sparse_bytes / 1024

    fig, axes = plt.subplots(1, 2, figsize=(13, 5), facecolor='#F8F9FA')

    ax = axes[0]
    ax.set_facecolor('#FFFFFF')
    cats = ['Flat 1-level\n(full map)', 'N-level\n(sparse active map)']
    vals = [flat_pb, sparse_bytes / (1024**5)]
    ax.bar(cats, vals, color=['#E74C3C', '#2ECC71'])
    ax.set_yscale('log')
    ax.set_ylabel('Page-table memory (PB, log scale)')
    ax.set_title('Scalability Gain from Hierarchical Paging')
    ax.grid(axis='y', linestyle='--', alpha=0.3)
    ax.text(0, vals[0], f'{flat_pb:.1f} PB', ha='center', va='bottom', fontsize=8)
    ax.text(1, vals[1], f'{sparse_kb:.0f} KB\n(for {active_pages} active pages)', ha='center', va='bottom', fontsize=8)

    ax2 = axes[1]
    ax2.set_facecolor('#FFFFFF')
    levels = ['PGD', 'P4D', 'PUD', 'PMD', 'PT']
    visited = [1, 1, 1, 1, 1]
    ax2.bar(levels, visited, color='#3498DB')
    ax2.set_ylim(0, 1.4)
    ax2.set_ylabel('Lookup step')
    ax2.set_title('Per-translation Walk Depth (N levels)')
    for i, lv in enumerate(levels):
        ax2.text(i, 1.05, lv, ha='center', fontsize=8, color='white', fontweight='bold')
    ax2.grid(axis='y', linestyle='--', alpha=0.3)

    fig.suptitle('Benefits of Extending Paging to N Levels', fontsize=12, fontweight='bold')
    plt.tight_layout(rect=[0, 0, 1, 0.93])
    plt.savefig(f'{OUT}/q5_viz.png', dpi=200, bbox_inches='tight', facecolor='white')
    plt.close()
    sanitize_png(f'{OUT}/q5_viz.png')
    print(f"Generated {OUT}/q5_viz.png")


def generate_q7_race():
    fig, axes = plt.subplots(2, 1, figsize=(13, 6.6), facecolor='#F8F9FA')
    fig.suptitle('Synchronization Impact on free_fp_list', fontsize=12, fontweight='bold')

    ax = axes[0]
    ax.set_facecolor('#FFFFFF')
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 4)
    ax.axis('off')
    ax.set_title('Before lock: race condition (double-claim risk)', fontsize=10, color='#922B21')
    ax.annotate('', xy=(11.5, 3), xytext=(0.5, 3), arrowprops=dict(arrowstyle='->', color='#2C3E50'))
    ax.annotate('', xy=(11.5, 1.5), xytext=(0.5, 1.5), arrowprops=dict(arrowstyle='->', color='#2C3E50'))
    ax.text(0.1, 3, 'CPU0', va='center', fontsize=9, fontweight='bold')
    ax.text(0.1, 1.5, 'CPU1', va='center', fontsize=9, fontweight='bold')
    ax.add_patch(mpatches.FancyBboxPatch((1, 2.7), 3.2, 0.55, boxstyle='round,pad=0.02', facecolor='#3498DB', edgecolor='white'))
    ax.add_patch(mpatches.FancyBboxPatch((2, 1.2), 3.2, 0.55, boxstyle='round,pad=0.02', facecolor='#E74C3C', edgecolor='white'))
    ax.text(2.6, 2.98, 'read Frame 5', ha='center', va='center', fontsize=8, color='white', fontweight='bold')
    ax.text(3.6, 1.48, 'read Frame 5', ha='center', va='center', fontsize=8, color='white', fontweight='bold')
    ax.text(6.4, 0.7, 'Both CPUs can claim same frame -> corruption', ha='center', fontsize=8.5, color='#E74C3C', fontweight='bold')

    ax2 = axes[1]
    ax2.set_facecolor('#FFFFFF')
    ax2.set_xlim(0, 12)
    ax2.set_ylim(0, 4)
    ax2.axis('off')
    ax2.set_title('After lock: serialized access', fontsize=10, color='#1E8449')
    ax2.annotate('', xy=(11.5, 3), xytext=(0.5, 3), arrowprops=dict(arrowstyle='->', color='#2C3E50'))
    ax2.annotate('', xy=(11.5, 1.5), xytext=(0.5, 1.5), arrowprops=dict(arrowstyle='->', color='#2C3E50'))
    ax2.text(0.1, 3, 'CPU0', va='center', fontsize=9, fontweight='bold')
    ax2.text(0.1, 1.5, 'CPU1', va='center', fontsize=9, fontweight='bold')
    ax2.add_patch(mpatches.FancyBboxPatch((1, 2.7), 4.2, 0.55, boxstyle='round,pad=0.02', facecolor='#2ECC71', edgecolor='white'))
    ax2.add_patch(mpatches.FancyBboxPatch((5.4, 1.2), 4.2, 0.55, boxstyle='round,pad=0.02', facecolor='#27AE60', edgecolor='white'))
    ax2.text(3.1, 2.98, 'lock -> get Frame 5 -> unlock', ha='center', va='center', fontsize=8, color='white', fontweight='bold')
    ax2.text(7.5, 1.48, 'lock -> get Frame 6 -> unlock', ha='center', va='center', fontsize=8, color='white', fontweight='bold')
    ax2.text(6.2, 0.7, 'No double allocation, no free list corruption', ha='center', fontsize=8.5, color='#1E8449', fontweight='bold')

    plt.tight_layout(rect=[0, 0, 1, 0.93])
    plt.savefig(f'{OUT}/q7_race.png', dpi=200, bbox_inches='tight', facecolor='white')
    plt.close()
    sanitize_png(f'{OUT}/q7_race.png')
    print(f"Generated {OUT}/q7_race.png")


# ══════════════════════════════════════════════════════════════════════════════
# MAIN
# ══════════════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    generate_gantts()
    generate_terminal_pic()
    generate_terminal_sec82()
    generate_q1_viz()
    generate_q3_viz()
    generate_watchdog_pic()
    generate_seg_paging_pic()
    generate_q5_viz()
    generate_q7_race()
    print("\nAll images generated in Images/")
