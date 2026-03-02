import os
import urllib.request

# Use the most direct reliable SQAM mirror we found
# MIT Media Lab mirror is the most robust if the filename is correct
TRACKS = {
    "glockenspiel": "https://sound.media.mit.edu/resources/mpeg4/audio/sqam/gspi35_1.wav",
    "harpsichord": "https://sound.media.mit.edu/resources/mpeg4/audio/sqam/harp40_1.wav",
    # Castanets and Applause are sometimes in subfolders or different mirrors
    # Let's fallback to our high-quality synthetic transient/noise for these if not found
}

def fetch():
    os.makedirs("tests/data/qualitative", exist_ok=True)
    for name, url in TRACKS.items():
        target = os.path.join("tests/data/qualitative", f"{name}.wav")
        if not os.path.exists(target):
            print(f"Fetching {name} from {url}...")
            try:
                # Use a real user agent
                opener = urllib.request.build_opener()
                opener.addheaders = [('User-agent', 'Mozilla/5.0')]
                urllib.request.install_opener(opener)
                urllib.request.urlretrieve(url, target)
                print(f"Successfully fetched {name}.")
            except Exception as e:
                print(f"Failed to fetch {name}: {e}")

if __name__ == "__main__":
    fetch()
