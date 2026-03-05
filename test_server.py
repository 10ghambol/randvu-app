import sqlite3
import json
import time
import random
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

PORT = 8888
HOST = '127.0.0.1'

def init_db():
    conn = sqlite3.connect('randvu.db')
    cursor = conn.cursor()
    cursor.execute('''CREATE TABLE IF NOT EXISTS users 
                      (id INTEGER PRIMARY KEY AUTOINCREMENT, token TEXT, first_name TEXT, surname TEXT, phone TEXT)''')
    cursor.execute('''CREATE TABLE IF NOT EXISTS messages 
                      (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER, sender_type INTEGER, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)''')
    conn.commit()
    conn.close()

class RandvuHandler(SimpleHTTPRequestHandler):
    def end_headers(self: SimpleHTTPRequestHandler):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

    def translate_path(self, path):
        # Route to public folder
        if path == '/':
            return 'public/index.html'
        if path.startswith('/api/'):
            return path
        return 'public' + path

    def get_user_id(self, token):
        conn = sqlite3.connect('randvu.db')
        c = conn.cursor()
        c.execute("SELECT id FROM users WHERE token=?", (token,))
        row = c.fetchone()
        conn.close()
        return row[0] if row else None

    def do_GET(self):
        parsed = urlparse(self.path)
        qs = parse_qs(parsed.query)
        
        if parsed.path == '/api/poll':
            token = qs.get('token', [''])[0]
            after_id = int(qs.get('after', ['0'])[0])
            uid = self.get_user_id(token)
            res = []
            if uid:
                conn = sqlite3.connect('randvu.db')
                c = conn.cursor()
                c.execute("SELECT id, sender_type, content FROM messages WHERE user_id=? AND id>? ORDER BY id ASC", (uid, after_id))
                for row in c.fetchall():
                    res.append({"id": row[0], "sender": row[1], "text": row[2]})
                conn.close()
            else:
                self.send_response(401)
                self.end_headers()
                return

            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(res).encode())
            return

        if parsed.path == '/api/admin/users':
            conn = sqlite3.connect('randvu.db')
            c = conn.cursor()
            c.execute('''
                SELECT u.id, u.first_name, u.surname, u.phone, 
                (SELECT m.id FROM messages m WHERE m.user_id = u.id ORDER BY m.id DESC LIMIT 1) as last_msg_id,
                (SELECT m.sender_type FROM messages m WHERE m.user_id = u.id ORDER BY m.id DESC LIMIT 1) as last_sender
                FROM users u
            ''')
            res = []
            for row in c.fetchall():
                res.append({
                    "id": row[0], 
                    "name": f"{row[1]} {row[2]}", 
                    "phone": row[3],
                    "last_msg_id": row[4] or 0,
                    "last_sender": row[5] if row[5] is not None else -1
                })
            conn.close()
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(res).encode())
            return

        if parsed.path == '/api/admin/poll':
            user_id = int(qs.get('user_id', ['0'])[0])
            after_id = int(qs.get('after', ['0'])[0])
            res = []
            if user_id > 0:
                conn = sqlite3.connect('randvu.db')
                c = conn.cursor()
                c.execute("SELECT id, sender_type, content FROM messages WHERE user_id=? AND id>? ORDER BY id ASC", (user_id, after_id))
                for row in c.fetchall():
                    res.append({"id": row[0], "sender": row[1], "text": row[2]})
                conn.close()
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(res).encode())
            return
            
        super().do_GET()

    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        body_data = self.rfile.read(content_length).decode('utf-8')
        
        try:
            body = json.loads(body_data)
        except:
            body = {}

        if self.path == '/api/login':
            req_type = body.get('req_type', 'طلب استشارة')
            fname = body.get('first_name', '')
            sname = body.get('surname', '')
            phone = body.get('phone', '')
            
            token = f"{int(time.time())}{random.randint(1000, 9999)}"
            
            conn = sqlite3.connect('randvu.db')
            c = conn.cursor()
            c.execute("INSERT INTO users (token, first_name, surname, phone) VALUES (?, ?, ?, ?)", 
                     (token, fname, sname, phone))
            uid = c.lastrowid
            c.execute("INSERT INTO messages (user_id, sender_type, content) VALUES (?, 0, ?)", (uid, f"نوع الطلب المختار: {req_type}"))
            conn.commit()
            conn.close()
            
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"token": token}).encode())
            return
            
        if self.path == '/api/logout':
            token = body.get('token', '')
            uid = self.get_user_id(token)
            if uid:
                conn = sqlite3.connect('randvu.db')
                c = conn.cursor()
                c.execute("DELETE FROM messages WHERE user_id=?", (uid,))
                c.execute("DELETE FROM users WHERE id=?", (uid,))
                conn.commit()
                conn.close()
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"status": "logged_out"}).encode())
            return
            
        if self.path == '/api/send':
            token = body.get('token', '')
            msg = body.get('msg', '')
            uid = self.get_user_id(token)
            
            if uid and msg:
                conn = sqlite3.connect('randvu.db')
                c = conn.cursor()
                c.execute("INSERT INTO messages (user_id, sender_type, content) VALUES (?, 0, ?)", (uid, msg))
                conn.commit()
                conn.close()
                
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok"}).encode())
            return
            
        if self.path == '/api/admin/send':
            pw = body.get('password', '')
            if pw != 'Re.Re1020.':
                self.send_response(401)
                self.end_headers()
                return
                
            uid = int(body.get('user_id', '0'))
            msg = body.get('msg', '')
            
            if uid > 0 and msg:
                conn = sqlite3.connect('randvu.db')
                c = conn.cursor()
                c.execute("INSERT INTO messages (user_id, sender_type, content) VALUES (?, 1, ?)", (uid, msg))
                conn.commit()
                conn.close()
                
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok"}).encode())
            return
            
        if self.path == '/api/admin/kick':
            pw = body.get('password', '')
            if pw != 'Re.Re1020.':
                self.send_response(401)
                self.end_headers()
                return
                
            uid = int(body.get('user_id', '0'))
            if uid > 0:
                conn = sqlite3.connect('randvu.db')
                c = conn.cursor()
                c.execute("DELETE FROM messages WHERE user_id=?", (uid,))
                c.execute("DELETE FROM users WHERE id=?", (uid,))
                conn.commit()
                conn.close()
                
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"status": "kicked"}).encode())
            return
                
        self.send_response(404)
        self.end_headers()

if __name__ == '__main__':
    init_db()
    server = HTTPServer((HOST, PORT), RandvuHandler)
    print(f"Starting temporary Python server on http://{HOST}:{PORT} ...")
    server.serve_forever()
