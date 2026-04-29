import hmac
import os
import threading
from datetime import datetime

import paho.mqtt.client as mqtt
import requests as http_requests
from bson import ObjectId
from flask import (
    Flask,
    abort,
    jsonify,
    redirect,
    render_template,
    request,
    session,
    url_for,
)
from pymongo import MongoClient
from pymongo.errors import ConnectionFailure

# ============================================================
# Flask App & Session Config
# ============================================================
app = Flask(__name__)

# SECRET_KEY wajib di-set via environment variable (OWASP A02)
app.secret_key = os.environ.get(
    "SECRET_KEY", "GANTI_DENGAN_SECRET_KEY_RANDOM_YANG_PANJANG"
)
app.config["SESSION_COOKIE_HTTPONLY"] = True  # Cegah akses JS ke cookie (OWASP A07)
app.config["SESSION_COOKIE_SAMESITE"] = "Lax"  # Mitigasi CSRF (OWASP A01)

# ============================================================
# Kredensial Admin (Single-User Auth)
# Ambil dari environment variable, JANGAN hardcode di sini.
# ============================================================
ADMIN_USERNAME = os.environ.get("ADMIN_USERNAME", "admin")
ADMIN_PASSWORD = os.environ.get("ADMIN_PASSWORD", "GANTI_DENGAN_PASSWORD_ANDA")

# ============================================================
# Konfigurasi MongoDB
# Database: smart_box | Collection: packages
# ============================================================
MONGO_URI = os.environ.get("MONGO_URI", "mongodb://localhost:27017/smart_box")
mongo_client = MongoClient(MONGO_URI, serverSelectionTimeoutMS=5000)
db = mongo_client["smart_box"]
packages_col = db["packages"]

# ============================================================
# Konfigurasi MQTT (broker.avisha.id)
# ============================================================
MQTT_BROKER = "broker.avisha.id"
MQTT_PORT = 1883
MQTT_USERNAME = "barka"
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "GANTI_DENGAN_PASSWORD_ANDA")
MQTT_TOPIC_KONTROL = "barka/kontrol"

mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="keemabox-server")
mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)


def on_connect(client, userdata, flags, rc, properties):
    if rc == 0:
        print("MQTT Connected ke broker.avisha.id")
    else:
        print(f"MQTT Connection FAILED, return code: {rc}")


def on_disconnect(client, userdata, flags, rc, properties):
    print(f"MQTT Disconnected, return code: {rc}")


mqtt_client.on_connect = on_connect
mqtt_client.on_disconnect = on_disconnect

# ============================================================
# Konfigurasi Telegram Bot
# Set via environment variable:
#   export TELEGRAM_BOT_TOKEN="123456:ABC-DEF..."
#   export TELEGRAM_CHAT_ID="-100xxxxxxxxxx"
# ============================================================
TELEGRAM_BOT_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN", "")
TELEGRAM_CHAT_ID = os.environ.get("TELEGRAM_CHAT_ID", "")


def send_telegram_notification(resi, barang):
    """Kirim notifikasi ke Telegram saat resi tervalidasi."""
    if not TELEGRAM_BOT_TOKEN or not TELEGRAM_CHAT_ID:
        print("Telegram: TOKEN atau CHAT_ID belum di-set, notifikasi dilewati.")
        return False

    pesan = (
        "📦 *PAKET TIBA!*\n"
        "━━━━━━━━━━━━━━━\n"
        f"📋 Barang: *{barang}*\n"
        f"🔖 Resi: `{resi}`\n"
        f"🕐 Waktu: {datetime.now().strftime('%d-%m-%Y %H:%M:%S')}\n"
        "━━━━━━━━━━━━━━━\n"
        "Kurir sedang menunggu. Silakan buka Dashboard untuk membuka pintu kotak."
    )

    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    payload = {"chat_id": TELEGRAM_CHAT_ID, "text": pesan, "parse_mode": "Markdown"}

    try:
        resp = http_requests.post(url, json=payload, timeout=10)
        if resp.status_code == 200:
            print(f"Telegram: Notifikasi terkirim (resi: {resi})")
            return True
        else:
            print(f"Telegram: Gagal kirim, status {resp.status_code} - {resp.text}")
            return False
    except Exception as e:
        print(f"Telegram: Error - {e}")
        return False


# ============================================================
# Helper Auth
# ============================================================
def is_logged_in():
    """Cek apakah sesi admin aktif."""
    return session.get("logged_in") is True


# ============================================================
# Routes — Public Portal
# ============================================================


@app.route("/")
def kurir():
    """
    Portal Publik — halaman pertama yang dilihat siapapun.
    Hanya menampilkan form input resi untuk kurir.
    """
    return render_template("kurir.html")


# ============================================================
# Routes — Login (Security through Obscurity: URL tidak obvious)
# ============================================================


@app.route("/dca-dooa-001", methods=["GET", "POST"])
def login():
    """Halaman login rahasia untuk admin."""
    if request.method == "GET":
        # Jika sudah login, langsung redirect ke dashboard
        if is_logged_in():
            return redirect(url_for("dashboard"))
        return render_template("login.html", error=None)

    # POST: Validasi kredensial
    username = request.form.get("username", "").strip()
    password = request.form.get("password", "").strip()

    # Gunakan hmac.compare_digest untuk mencegah Timing Attack (OWASP A07)
    username_ok = hmac.compare_digest(username, ADMIN_USERNAME)
    password_ok = hmac.compare_digest(password, ADMIN_PASSWORD)

    if username_ok and password_ok:
        session.clear()  # Bersihkan sesi lama (Session Fixation)
        session["logged_in"] = True
        session.permanent = False
        print(f"Login: Admin berhasil login dari IP {request.remote_addr}.")
        return redirect(url_for("dashboard"))
    else:
        print(
            f"Login: Percobaan gagal dari IP {request.remote_addr} (username: '{username}')."
        )
        return render_template("login.html", error="Username atau password salah.")


# ============================================================
# Routes — Dashboard Owner (Fake 404 Trap)
# ============================================================


@app.route("/dca-dooa-002")
def dashboard():
    """
    Dashboard Owner — hanya bisa diakses jika sudah login.
    Fake 404 Trap: jika belum login, tampilkan 404 bukan redirect ke login.
    Ini mencegah attacker mengetahui bahwa halaman ini ada.
    """
    if not is_logged_in():
        abort(404)  # Fake 404 — tidak mengungkap keberadaan rute ini
    return render_template("index.html")


# ============================================================
# Routes — Logout
# ============================================================


@app.route("/dca-dooa-logot")
def logout():
    """Hapus sesi dan kembali ke Portal Publik."""
    session.clear()
    print("Logout: Admin logout.")
    return redirect(url_for("kurir"))


# ============================================================
# Routes — API
# ============================================================


@app.route("/api/status")
def api_status():
    """Health check & status koneksi (dipakai frontend)."""
    try:
        mongo_client.admin.command("ping")
        db_status = "MongoDB Connected"
    except ConnectionFailure:
        db_status = "MongoDB Connection FAILED"

    mqtt_status = "Connected" if mqtt_client.is_connected() else "Disconnected"

    return jsonify(
        {
            "project": "KeeMaBox - Smart Delivery Box",
            "status": "Server Running",
            "database": db_status,
            "mqtt": mqtt_status,
        }
    )


@app.route("/api/packages")
def api_packages():
    """Ambil daftar semua paket dari MongoDB. Hanya untuk admin."""
    if not is_logged_in():
        abort(404)
    pkgs = list(packages_col.find().sort("_id", -1))
    for p in pkgs:
        p["_id"] = str(p["_id"])
        if "validated_at" in p and p["validated_at"]:
            p["validated_at"] = p["validated_at"].isoformat()
    return jsonify({"packages": pkgs})


@app.route("/api/tambah_paket", methods=["POST"])
def tambah_paket():
    """Daftarkan paket baru ke MongoDB. Hanya untuk admin."""
    if not is_logged_in():
        abort(404)

    data = request.get_json(silent=True)
    if not data or "nama_barang" not in data or "resi_number" not in data:
        return jsonify(
            {
                "success": False,
                "message": "Field 'nama_barang' dan 'resi_number' wajib diisi.",
            }
        ), 400

    nama_barang = data["nama_barang"].strip()
    resi_number = data["resi_number"].strip()

    if not nama_barang or not resi_number:
        return jsonify(
            {
                "success": False,
                "message": "Nama barang dan nomor resi tidak boleh kosong.",
            }
        ), 400

    # Cek duplikat resi
    if packages_col.find_one({"resi_number": resi_number}):
        return jsonify(
            {"success": False, "message": "Nomor resi sudah terdaftar."}
        ), 409

    packages_col.insert_one(
        {
            "nama_barang": nama_barang,
            "resi_number": resi_number,
            "status": "pending",
            "created_at": datetime.now(),
        }
    )
    print(f"Paket Baru: '{nama_barang}' (resi: {resi_number}) -> Terdaftar.")

    return jsonify(
        {
            "success": True,
            "message": "Paket berhasil didaftarkan.",
            "data": {
                "nama_barang": nama_barang,
                "resi_number": resi_number,
                "status": "pending",
            },
        }
    )


@app.route("/api/edit_paket/<id>", methods=["PUT", "POST"])
def edit_paket(id):
    """Edit nama_barang dan resi_number paket. Hanya untuk admin."""
    if not is_logged_in():
        abort(404)

    try:
        obj_id = ObjectId(id)
    except Exception:
        return jsonify({"success": False, "message": "ID tidak valid."}), 400

    data = request.get_json(silent=True)
    if not data:
        return jsonify({"success": False, "message": "Body JSON wajib diisi."}), 400

    update_fields = {}
    if "nama_barang" in data and data["nama_barang"].strip():
        update_fields["nama_barang"] = data["nama_barang"].strip()
    if "resi_number" in data and data["resi_number"].strip():
        # Cek duplikat resi (kecuali milik sendiri)
        existing = packages_col.find_one(
            {"resi_number": data["resi_number"].strip(), "_id": {"$ne": obj_id}}
        )
        if existing:
            return jsonify(
                {"success": False, "message": "Nomor resi sudah dipakai paket lain."}
            ), 409
        update_fields["resi_number"] = data["resi_number"].strip()

    if not update_fields:
        return jsonify(
            {"success": False, "message": "Tidak ada data yang diubah."}
        ), 400

    update_fields["updated_at"] = datetime.now()
    result = packages_col.update_one({"_id": obj_id}, {"$set": update_fields})

    if result.matched_count == 0:
        return jsonify({"success": False, "message": "Paket tidak ditemukan."}), 404

    print(f"Edit Paket: ID {id} -> {update_fields}")
    return jsonify({"success": True, "message": "Paket berhasil diperbarui."})


@app.route("/api/hapus_paket/<id>", methods=["DELETE"])
def hapus_paket(id):
    """Hapus paket dari MongoDB. Hanya untuk admin."""
    if not is_logged_in():
        abort(404)

    try:
        obj_id = ObjectId(id)
    except Exception:
        return jsonify({"success": False, "message": "ID tidak valid."}), 400

    result = packages_col.delete_one({"_id": obj_id})

    if result.deleted_count == 0:
        return jsonify({"success": False, "message": "Paket tidak ditemukan."}), 404

    print(f"Hapus Paket: ID {id} -> Dihapus.")
    return jsonify({"success": True, "message": "Paket berhasil dihapus."})


@app.route("/api/validasi_resi", methods=["POST"])
def validasi_resi():
    """
    Menerima JSON { "resi_number": "..." }.
    Endpoint PUBLIK — dapat diakses kurir tanpa login.
    Cek ke MongoDB -> Jika ada, kirim notifikasi Telegram.
    """
    data = request.get_json(silent=True)
    if not data or "resi_number" not in data:
        return jsonify(
            {"success": False, "message": "Field 'resi_number' wajib diisi."}
        ), 400

    resi_number = data["resi_number"].strip()
    if not resi_number:
        return jsonify(
            {"success": False, "message": "Nomor resi tidak boleh kosong."}
        ), 400

    # Cari resi di collection packages
    paket = packages_col.find_one({"resi_number": resi_number})

    if not paket:
        print(f"Validasi: Resi '{resi_number}' TIDAK ditemukan di database.")
        return jsonify(
            {"success": False, "message": "Nomor resi tidak ditemukan."}
        ), 404

    # Resi ditemukan -> kirim notifikasi Telegram
    nama_barang = paket.get("nama_barang", "Tidak diketahui")
    send_telegram_notification(resi_number, nama_barang)

    # Update status paket
    packages_col.update_one(
        {"_id": paket["_id"]},
        {"$set": {"status": "validated", "validated_at": datetime.now()}},
    )
    print(f"Validasi: Resi '{resi_number}' VALID -> Notifikasi Telegram terkirim.")

    return jsonify(
        {
            "success": True,
            "message": "Resi valid! Notifikasi telah dikirim ke pemilik.",
            "data": {
                "resi_number": resi_number,
                "nama_barang": nama_barang,
                "status": "validated",
            },
        }
    )


@app.route("/api/buka_pintu", methods=["POST"])
def buka_pintu():
    """Publish perintah OPEN ke MQTT topic barka/kontrol. Hanya untuk admin."""
    if not is_logged_in():
        abort(404)

    if not mqtt_client.is_connected():
        print("Buka Pintu: MQTT tidak terkoneksi, gagal publish.")
        return jsonify(
            {"success": False, "message": "MQTT broker tidak terkoneksi."}
        ), 503

    result = mqtt_client.publish(MQTT_TOPIC_KONTROL, "OPEN", qos=1)
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        print(f"Buka Pintu: MQTT Publish 'OPEN' ke topic '{MQTT_TOPIC_KONTROL}' -> OK")
        return jsonify(
            {"success": True, "message": "Perintah OPEN berhasil dikirim ke kotak."}
        )
    else:
        print(f"Buka Pintu: MQTT Publish GAGAL, rc={result.rc}")
        return jsonify(
            {"success": False, "message": "Gagal mengirim perintah ke kotak."}
        ), 500


@app.route("/api/upload_frame", methods=["POST"])
def upload_frame():
    """
    Endpoint untuk menerima frame gambar dari ESP32-CAM.
    Akan diimplementasi penuh di Phase 5.
    """
    if "frame" not in request.files:
        return jsonify(
            {"success": False, "message": "File 'frame' tidak ditemukan dalam request."}
        ), 400

    frame_file = request.files["frame"]
    if frame_file.filename == "":
        return jsonify({"success": False, "message": "Nama file kosong."}), 400

    # TODO Phase 5: Simpan frame ke /static/uploads/ dan kompilasi ke MP4
    print(
        f"Upload Frame: Diterima '{frame_file.filename}' ({frame_file.content_length} bytes)"
    )

    return jsonify(
        {"success": True, "message": "Frame diterima.", "filename": frame_file.filename}
    )


# ============================================================
# MQTT Connect (lazy init via before_request)
# Menjamin MQTT connect di worker process yang benar,
# bukan di parent reloader Flask debug mode.
# ============================================================
import atexit

mqtt_started = False


def start_mqtt():
    """Jalankan MQTT client loop di background thread."""
    global mqtt_started
    if mqtt_started:
        return
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()
        mqtt_started = True
        atexit.register(lambda: mqtt_client.loop_stop())
        print(f"MQTT: Terhubung ke {MQTT_BROKER}:{MQTT_PORT}")
    except Exception as e:
        print(f"MQTT: Gagal connect - {e}")


@app.before_request
def ensure_mqtt():
    """Auto-connect MQTT saat request pertama masuk."""
    start_mqtt()


# ============================================================
# Main
# ============================================================
if __name__ == "__main__":
    print("=== KeeMaBox Flask Server Starting ===")
    print(f"MONGO_URI : {MONGO_URI}")
    print(f"MQTT      : {MQTT_BROKER}:{MQTT_PORT} (user: {MQTT_USERNAME})")
    print(f"TELEGRAM  : {'Configured' if TELEGRAM_BOT_TOKEN else 'NOT SET'}")
    print(
        f"ADMIN     : {'Configured' if ADMIN_PASSWORD != 'GANTI_DENGAN_PASSWORD_ANDA' else 'WARNING: Gunakan password dari env!'}"
    )

    app.run(host="0.0.0.0", port=5000, debug=True)
