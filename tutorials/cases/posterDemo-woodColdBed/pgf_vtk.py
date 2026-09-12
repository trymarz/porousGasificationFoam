# #!/usr/bin/env -S uv run --script
#
# /// script
# requires-python = ">=3.13"
# dependencies = [
#     "vtk",
# ]
# ///
import os
import glob
import vtk
import json


def timestamp_from_name(fname):
    """
    Extract the numeric timestamp part from a file name like:
    spheres‑rank0‑12.3456.vtp  ->  12.3456
    """
    base = os.path.basename(fname)
    # split on '-' and strip extension
    parts = base.split("-")
    ts_str = parts[-1].replace(".vtp", "")
    return float(ts_str)


# Build a dict: {timestamp: [list of files belonging to that ts]}
def build_ts_dict():
    files_by_ts = {}
    for fname in glob.glob("spheres/spheres-rank*-*.vtp"):
        ts = timestamp_from_name(fname)
        files_by_ts.setdefault(ts, []).append(fname)

    return files_by_ts


# Merge per‑timestamp
def merge_procs(merge_dir):
    files_by_ts = build_ts_dict()
    for ts, file_list in sorted(files_by_ts.items()):
        print(f"Merging {len(file_list)} files for t={ts}")

        # Create the append filter
        append = vtk.vtkAppendPolyData()

        # Read each piece and feed it to the filter
        for fpath in file_list:
            reader = vtk.vtkXMLPolyDataReader()
            reader.SetFileName(fpath)
            reader.Update()
            append.AddInputData(reader.GetOutput())

        # Execute the merge
        append.Update()

        # Write the merged result
        writer = vtk.vtkXMLPolyDataWriter()
        out_name = f"spheres_t{ts:.4f}.vtp"
        out_path = os.path.join(merge_dir, out_name)
        writer.SetFileName(out_path)
        writer.SetInputData(append.GetOutput())
        writer.SetDataModeToAscii()  # keep consistency with your original files
        writer.Write()


def merge_times(merge_dir: str):
    files = [f for f in os.listdir(merge_dir) if f.endswith(".vtp")]
    files.sort()

    file_list = []
    for f in files:
        time_str = f.replace("spheres_t", "").replace(".vtp", "")
        time_val = float(time_str)
        file_list.append({"name": f, "time": time_val})

    with open("merged/spheres.vtp.series", "w") as f:
        json.dump({"file-series-version": "1.0", "files": file_list}, f, indent=2)

    print("Created spheres.vtp.series with", len(file_list), "files.")


if __name__ == "__main__":
    merge_dir = "merged"
    os.makedirs(merge_dir, exist_ok=True)
    merge_procs(merge_dir)
    merge_times(merge_dir)
