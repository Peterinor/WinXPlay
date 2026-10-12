"""Local entry for the shared diagnostic packager; use the project Python runtime."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from package_diagnostics import main

if __name__=='__main__':main()
