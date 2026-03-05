import subprocess
import time

print("==============================================")
print("Fixing GitHub Link for the New Account")
print("==============================================")

# This is the exact link for your new account 10ghambol
new_url = "https://github.com/10ghambol/randvu-app.git"

print(f"\n[.] Setting GitHub URL to: {new_url}")
subprocess.run(["git", "remote", "set-url", "origin", new_url], check=False)

print("\n[.] Pushing to the new account so the site updates...")
subprocess.run(["git", "push", "-u", "origin", "main", "--force"], check=False)

print("\n==============================================")
print("Done! You can use backup_to_github.bat directly next time.")
print("==============================================")
time.sleep(15)
