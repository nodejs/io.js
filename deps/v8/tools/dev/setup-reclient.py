#!/usr/bin/env python3
# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""\
Convenience script to set up reclient (aka "remoteexec").
"""

from enum import IntEnum
from pathlib import Path
import os
import platform
import re
import subprocess

V8_DIR = Path(__file__).resolve().parent.parent.parent
GCLIENT_FILE_PATH = V8_DIR.parent / ".gclient"

RBE_INSTANCE = "projects/rbe-chromium-untrusted/instances/default_instance"


class MachineType(IntEnum):
  EXTERNAL = 0
  GOOGLE_DESKTOP = 1
  GOOGLE_LAPTOP = 2


MACHINE_TYPE_NAME = {
    MachineType.EXTERNAL: "External",
    MachineType.GOOGLE_DESKTOP: "Google Desktop",
    MachineType.GOOGLE_LAPTOP: "Google Laptop",
}


def DetectShell():
  # We could just read the SHELL environment variable, but that can be
  # inaccurate when users use multiple shells. So instead, detect the
  # shell that this script was called from.
  pid = subprocess.check_output(f"ps -p {os.getpid()} -o ppid=", shell=True)
  pid = pid.decode("utf-8").strip()
  if platform.system() == "Linux":
    shell = subprocess.check_output(["readlink", "-f", f"/proc/{pid}/exe"])
    return shell.decode("utf-8").strip()
  elif platform.system() == "Darwin":
    shell = subprocess.check_output(
        f"lsof -p {pid} | grep txt | head -1", shell=True)
    return shell.decode("utf-8").strip().split(" ")[-1]
  else:
    raise Exception(f"Unimplemented platform: {platform.system()}")


SHELL = DetectShell()


def InstallGCloudPublic():
  existing = subprocess.call("which gcloud", shell=True)
  if existing == 0:
    print("GCloud is already installed, great.")
    return False
  print("Downloading and running GCloud installer. "
        "Please let it add the CLI tools to your PATH when prompted.")
  subprocess.check_call(
      "curl -o gcloud-install.sh https://sdk.cloud.google.com", shell=True)
  subprocess.check_call("bash gcloud-install.sh", shell=True)
  subprocess.check_call("rm gcloud-install.sh", shell=True)
  return True


def UpdateGCloud():
  try:
    # Use "SHELL -i" to consider any updated .bashrc or .zshrc.
    # Use "SHELL -l" to consider any updated .bash_profile or .zsh_profile.
    gcloud_version = subprocess.check_output(
        f"{SHELL} -il -c 'gcloud version 2>/dev/null; exit 0'", shell=True)
    gcloud_version = gcloud_version.decode("utf-8").strip()
    gcloud_version = int(
        re.search(r"Google Cloud SDK (\d+)\.\d+\.\d+", gcloud_version).group(1))
    print(f"Detected gcloud version: {gcloud_version}")
  except Exception as e:
    print(f"{e}")
    print("Error running 'gcloud version'. "
          "Please fix your gcloud installation manually.")
    return False
  if gcloud_version < 455:
    print("Found old gcloud, updating.")
    subprocess.check_call(
        f"{SHELL} -il -c 'gcloud components update; exit 0'", shell=True)
  return True


def UpdateGclientFile(mode):
  if not GCLIENT_FILE_PATH.exists():
    print(f".gclient not found, bug in this script?")
    return False
  with open(GCLIENT_FILE_PATH) as f:
    content = f.read()
  try:
    config_dict = {}
    # .gclient is a Python file, so execute it to understand it.
    exec(content, config_dict)
  except SyntaxError:
    print(f"Error parsing .gclient: {content}")
    return False
  v8_solution_found = False
  for solution in config_dict["solutions"]:
    if (solution["name"] == "v8" or
        solution["url"] == "https://chromium.googlesource.com/v8/v8.git"):
      v8_solution_found = True
      if "custom_vars" in solution:
        custom_vars = solution["custom_vars"]
        if custom_vars.get("download_remoteexec_cfg", False) == True:
          if (mode == MachineType.GOOGLE_DESKTOP or
              custom_vars.get("rbe_instance", "") == RBE_INSTANCE):
            # Nothing to do.
            return True
      else:
        custom_vars = solution["custom_vars"] = {}
      custom_vars["download_remoteexec_cfg"] = True
      if mode == MachineType.EXTERNAL:
        custom_vars["rbe_instance"] = RBE_INSTANCE
  if not v8_solution_found:
    print("Error: v8 solution not found in .gclient")
    return False

  # Imitate the format that depot_tools/fetch.py produces.
  # It's not quite JSON and it's not what Python's "pprint" would do.
  def _format_literal(lit):
    if isinstance(lit, str):
      return f'"{lit}"'
    if isinstance(lit, list):
      body = ", ".join(_format_literal(i) for i in lit)
      return f"[{body}]"
    if isinstance(lit, dict):
      if not lit.items():
        return "{}"
      body = "{\n"
      for key, value in lit.items():
        body += f'      "{key}": {_format_literal(value)},\n'
      body += "    }"
      return body
    return f"{lit!r}"

  new_content = "solutions = [\n"
  for s in config_dict["solutions"]:
    new_content += "  {\n"
    for key, value in s.items():
      new_content += f'    "{key}": {_format_literal(value)},\n'
    new_content += "  },\n"
  new_content += "]\n"
  for entry, value in config_dict.items():
    if entry == "solutions" or entry.startswith("__"):
      continue
    new_content += f"{entry} = {_format_literal(value)}\n"
  with open(GCLIENT_FILE_PATH, "w") as f:
    f.write(new_content)
  return True


def PromptLogin(mode):
  domain = "chromium.org"
  if mode in (MachineType.GOOGLE_DESKTOP, MachineType.GOOGLE_LAPTOP):
    domain = "google.com"
  print(f"Please log in with your {domain} account. "
        "Press <Return> to continue...")
  input()


def CipdLogin(mode):
  PromptLogin(mode)
  subprocess.check_call("cipd auth-login", shell=True)
  subprocess.check_call("gclient sync -D", shell=True)


def GCloudLogin(mode):
  PromptLogin(mode)
  # Use "SHELL -il" to consider any updated .bashrc and .bash_profile.
  subprocess.check_call(
      f"{SHELL} -il -c 'gcloud auth login --update-adc; exit 0'", shell=True)


def SetupLinux():
  mode = MachineType.EXTERNAL
  LSB_RELEASE = Path("/etc/lsb-release")
  if LSB_RELEASE.exists():
    with open(LSB_RELEASE) as f:
      lsb_release = f.read()
      if "GOOGLE_ROLE=desktop" in lsb_release:
        mode = MachineType.GOOGLE_DESKTOP
      elif "GOOGLE_ROLE=laptop" in lsb_release:
        mode = MachineType.GOOGLE_LAPTOP
  print(f"Detected machine type: {MACHINE_TYPE_NAME[mode]}")

  if not UpdateGclientFile(mode):
    return
  CipdLogin(mode)

  # Google desktops: gcert will be sufficient for authentication.
  # Google laptops: install gcloud via apt.
  if mode == MachineType.GOOGLE_LAPTOP:
    print("Installing gcloud, please authenticate for 'sudo':")
    subprocess.check_call("sudo apt install -y google-cloud-cli", shell=True)
    if not UpdateGCloud():
      return
    GCloudLogin(mode)
  # Non-Google machines: install gcloud the public way.
  elif mode == MachineType.EXTERNAL:
    gcloud_installed = InstallGCloudPublic()
    if not UpdateGCloud():
      return
    GCloudLogin(mode)
    if gcloud_installed:
      print("GCloud SDK was installed, remember to 'source ~/.bashrc' to "
            "make your updated PATH available to the current shell.")
  print("All done, enjoy fast builds!")


def SetupMac():
  mode = MachineType.EXTERNAL
  if Path("/usr/local/google").exists():
    print(
        "Looks like this could be a Google corp machine, is that correct? "
        "[Y/n] ",
        end="")
    while True:
      answer = input()
      if answer == "" or answer.lower() == "y":
        mode = MachineType.GOOGLE_LAPTOP
        break
      if answer.lower() == "n":
        break
      print(f"Answer '{answer}' not understood, please say 'y' or 'n'.")
  print(f"Machine type: {MACHINE_TYPE_NAME[mode]}")

  if not UpdateGclientFile(mode):
    return
  CipdLogin(mode)

  InstallGCloudPublic()
  if not UpdateGCloud():
    return
  GCloudLogin(mode)
  print("All done, enjoy fast builds!")


if __name__ == "__main__":
  if platform.system() == "Linux":
    SetupLinux()
  elif platform.system() == "Darwin":
    SetupMac()
  else:
    print("Platforms other than Linux and Mac are not yet implemented, sorry")
