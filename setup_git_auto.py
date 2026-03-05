import os
import subprocess
import time

def init_git_repo():
    print("==============================================")
    print("Randvu Project Auto-Git Setup")
    print("==============================================")
    print("\n[.] Initializing Git and configuring remote...")
    
    # 1. Initialize git
    subprocess.run(["git", "init"], check=False)
    
    # 2. Add files
    subprocess.run(["git", "add", "."], check=False)
    
    # 2.5 Configure Git identity (Fix for 'Author identity unknown')
    subprocess.run(["git", "config", "user.email", "admin@randvu.local"], check=False)
    subprocess.run(["git", "config", "user.name", "Randvu Admin"], check=False)
    
    # 3. Commit
    subprocess.run(["git", "commit", "-m", "Auto-setup commit"], check=False)
    
    # 4. Set branch to main
    subprocess.run(["git", "branch", "-M", "main"], check=False)
    
    # 5. Ask user for their Repository URL dynamically
    print("\n[?] We need your GitHub Repository URL to connect it to your account.")
    print("    1. Go to your GitHub project in your browser.")
    print("    2. Click the green 'Code' button and copy the HTTPS URL.")
    print("       (It looks like: https://github.com/Username/randvu-app.git)")
    repo_url = input("\n[>] Please Paste your GitHub Repo URL here and press Enter: ").strip()
    
    if not repo_url:
        print("[!] No URL provided. Setup cancelled.")
        return
        
    result = subprocess.run(["git", "remote", "add", "origin", repo_url], capture_output=True, text=True)
    if "already exists" in result.stderr:
        subprocess.run(["git", "remote", "set-url", "origin", repo_url], check=False)
    
    # 6. Push to github
    print("\n[.] Pushing code to GitHub (A browser window might open asking you to Sign In)...")
    push_result = subprocess.run(["git", "push", "-u", "origin", "main", "--force"], check=False)
    
    print("\n==============================================")
    print("Setup Complete! You can now use backup_to_github.bat")
    print("==============================================")
    time.sleep(10)

if __name__ == "__main__":
    init_git_repo()
