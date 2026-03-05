import os
import subprocess
import time

def clear_git_credentials():
    print("==============================================")
    print("Randvu Project - Clear Old GitHub Account")
    print("==============================================")
    print("\n[.] Removing old cached GitHub credentials from Windows...")
    
    # 1. Reject cached credentials manager temporarily or erase it
    try:
        # This command deletes the saved github.com credentials in Windows Credential Manager
        subprocess.run('cmdkey /delete:LegacyGeneric:target=git:https://github.com', shell=True, check=False, capture_output=True)
        subprocess.run('cmdkey /delete:git:https://github.com', shell=True, check=False, capture_output=True)
    except Exception as e:
        print(f"Warning: {e}")

    # 2. Tell git to unset the credential helper locally just in case
    subprocess.run(["git", "config", "--system", "--unset", "credential.helper"], check=False, capture_output=True)
    subprocess.run(["git", "config", "--global", "--unset", "credential.helper"], check=False, capture_output=True)
    
    # Set it back to manager-core which forces a new login prompt
    subprocess.run(["git", "config", "--global", "credential.helper", "manager"], check=False, capture_output=True)

    print("\n[.] Old credentials cleared!")
    print("[.] Now we will try to push again. This time a window SHOULD pop up asking you to sign in.")
    print("    Make sure to sign in with your NEW account (10ghambol).")
    
    print("\n[.] Pushing code...")
    push_result = subprocess.run(["git", "push", "-u", "origin", "main", "--force"], check=False)
    
    print("\n==============================================")
    print("Done! If the push was successful, you can now use backup_to_github.bat normally.")
    print("==============================================")
    time.sleep(15)

if __name__ == "__main__":
    clear_git_credentials()
