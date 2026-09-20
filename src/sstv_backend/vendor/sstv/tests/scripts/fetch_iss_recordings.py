#!/usr/bin/env python3
"""Download the real ISS SSTV recordings used by tests/iss_recordings.rs.

The recordings are off-air captures by KG4AKV (Space Comms,
https://spacecomms.wordpress.com/iss-sstv-audio-recordings/), distributed as
a single zip archive. They are large and stay outside the git history: this
script fetches the archive and extracts the recordings the tests use into the
gitignored tests/assets/iss/ directory.

An already-downloaded archive can be passed as the first argument to skip the
download.
"""

import os
import shutil
import sys
import tempfile
import urllib.request
import zipfile

ARCHIVE_URL = (
    "https://www.dropbox.com/s/ljghu4pte455az7/"
    "ISS_SSTV_Audio_Recordings_Space_Comms_KG4AKV_wav.zip?dl=1"
)
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DESTINATION = os.path.join(REPO_ROOT, "tests", "assets", "iss")
RECORDINGS = {
    "Space_Comms_-_2015-04-12_-_0428_UTC_-_80th_Yuri_Gagarin_image_5.wav":
        "pd180-gagarin-80.wav",
    "Space_Comms_-_2015-07-19_-_0227_UTC_-_Apollo_Souz_American_and_USSR_flag.wav":
        "pd180-apollo-soyuz.wav",
    "Space_Comms_-_2016-04-12_-_2134_UTC_-_ARISS_1st_QSO_-_Astros_-_and_Kids_image_9.wav":
        "pd180-ariss-qso-astros.wav",
    "Space_Comms_-_2016-04-13_-_1904_UTC_-_ARISS_1st_QSO_-_Cristoforetti_Garriot_image_4.wav":
        "pd180-ariss-qso-cristoforetti.wav",
    "Space_Comms_-_2016-04-15_-_1856_UTC_-_MAI-75_-_SuitSat_image_9.wav":
        "pd180-mai75-suitsat.wav",
    "Space_Comms_-_2017-07-23 _-_0246_UTC_-_ARISS_20_Year_-_image_1.wav":
        "pd120-ariss-20-year-1.wav",
    "Space_Comms_-_2017-07-23 _-_0246_UTC_-_ARISS_20_Year_-_image_2.wav":
        "pd120-ariss-20-year-2.wav",
}


def extract(archive_path: str, missing: dict) -> None:
    with zipfile.ZipFile(archive_path) as archive:
        for member, name in missing.items():
            print(f"extracting {name}")
            with archive.open(member) as source:
                with open(os.path.join(DESTINATION, name), "wb") as out:
                    shutil.copyfileobj(source, out)


def main() -> None:
    os.makedirs(DESTINATION, exist_ok=True)
    missing = {
        member: name
        for member, name in RECORDINGS.items()
        if not os.path.exists(os.path.join(DESTINATION, name))
    }
    if not missing:
        print("all recordings present")
        return

    if len(sys.argv) > 1:
        extract(sys.argv[1], missing)
        return

    print(f"downloading {ARCHIVE_URL} (~130 MB)")
    request = urllib.request.Request(ARCHIVE_URL, headers={"User-Agent": "curl/8"})
    with tempfile.NamedTemporaryFile(suffix=".zip") as archive:
        with urllib.request.urlopen(request) as response:
            shutil.copyfileobj(response, archive)
        archive.flush()
        extract(archive.name, missing)
    print(f"recordings ready in {DESTINATION}")


if __name__ == "__main__":
    main()
