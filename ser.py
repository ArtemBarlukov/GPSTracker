from flask import Flask, request, jsonify
import requests
import telebot
import threading
import time

BOT_TOKEN = "8218306225:AAGpOBYoDrSGQrULdEhGh8OV8tLvd4XFsNY"
CHAT_ID = 735573352

bot = telebot.TeleBot(BOT_TOKEN, parse_mode="Markdown")
app = Flask(__name__)

last_data = {"lat": None, "lon": None, "sat": None}
auto_mode = False 


DEVICE_PHONE = "+79503834599" 
SMS_API_URL = "http://10.135.41.280:5000/send_sms"  

# ===================================
#   ПОСТОЯННОЕ МЕНЮ (Reply Keyboard)
# ===================================
def main_keyboard():
    kb = telebot.types.ReplyKeyboardMarkup(resize_keyboard=True)

    kb.row("📍 Получить координаты")
    kb.row("🔄 Авто ON", "⛔ Авто OFF")
    kb.row("🛰 Статус GPS")

    return kb


# ====================
#   ОТПРАВКА SMS НА УСТРОЙСТВО
# ====================
def send_sms_to_device(message):
    """Отправляет SMS на устройство через локальный API"""
    try:
        payload = {
            "phone": DEVICE_PHONE,
            "message": message
        }
        response = requests.post(SMS_API_URL, json=payload, timeout=5)
        return response.status_code == 200
    except Exception as e:
        print(f"Ошибка отправки SMS: {e}")
        return False


# ====================
#      /start
# ====================
@bot.message_handler(commands=["start"])
def start(message):
    bot.send_message(
        message.chat.id,
        "🛰 GPS Tracker Online\nВыберите действие:",
        reply_markup=main_keyboard()
    )


# ====================
#  ОБРАБОТКА КНОПОК
# ====================
@bot.message_handler(func=lambda msg: True)
def menu_handler(message):
    global last_data, auto_mode
    text = message.text

    if text == "📍 Получить координаты":
        if last_data["lat"] is None:
            bot.send_message(message.chat.id, "❌ Данных пока нет", reply_markup=main_keyboard())
            return

        bot.send_message(
            message.chat.id,
            f"📍 *Текущие координаты*\n"
            f"LAT: `{last_data['lat']}`\n"
            f"LON: `{last_data['lon']}`\n"
            f"🛰 SAT: `{last_data['sat']}`\n"
            f"https://maps.google.com/?q={last_data['lat']},{last_data['lon']}",
            reply_markup=main_keyboard()
        )
        


    # --- Авто ON ---
    elif text == "🔄 Авто ON":
        auto_mode = True
        bot.send_message(message.chat.id, "✅ Авто-режим включён", reply_markup=main_keyboard())
    

    # --- Авто OFF ---
    elif text == "⛔ Авто OFF":
        auto_mode = False
        bot.send_message(message.chat.id, "✅ Авто-режим выключен", reply_markup=main_keyboard())
        

    # --- Статус GPS ---
    elif text == "🛰 Статус GPS":
        if last_data["sat"] is None:
            bot.send_message(message.chat.id, "Нет данных", reply_markup=main_keyboard())
        else:
            status = f"🛰 Спутников: *{last_data['sat']}*\n"
            status += f"🤖 Авторежим: {'ВКЛ' if auto_mode else 'ВЫКЛ'}"
            
            bot.send_message(message.chat.id, status, reply_markup=main_keyboard())


# ==========================
#  АВТОМАТИЧЕСКАЯ ОТПРАВКА
# ==========================
def auto_send():
    """Автоматическая отправка координат в чат при включенном авторежиме"""
    global last_data, auto_mode
    
    while True:
        if auto_mode and last_data["lat"] is not None:
            try:
                bot.send_message(
                    CHAT_ID,
                    f"📍 *Авто-отправка*\n"
                    f"LAT: `{last_data['lat']}`\n"
                    f"LON: `{last_data['lon']}`\n"
                    f"🛰 SAT: `{last_data['sat']}`\n"
                    f"https://maps.google.com/?q={last_data['lat']},{last_data['lon']}",
                    reply_markup=main_keyboard()
                )
                print(f"[AUTO] Sent coordinates to Telegram")
            except Exception as e:
                print(f"[AUTO] Error sending to Telegram: {e}")
        
        # Ждем 5 минут (300 секунд) перед следующей отправкой
        time.sleep(300)


# ==========================
#         /update
# ==========================
@app.route("/update", methods=["POST"])
def update():
    global last_data, auto_mode
    data = request.json
    
    last_data.update(data)
    
    print(f"[UPDATE] Received: {data}")
    
    if auto_mode:
        try:
            bot.send_message(
                CHAT_ID,
                f"📍 *Новые данные (авто)*\n"
                f"LAT: `{data.get('lat', 'N/A')}`\n"
                f"LON: `{data.get('lon', 'N/A')}`\n"
                f"🛰 SAT: `{data.get('sat', 'N/A')}`",
                reply_markup=main_keyboard()
            )
        except Exception as e:
            print(f"[UPDATE] Error sending auto-notification: {e}")
    
    return jsonify({"status": "OK", "auto_mode": auto_mode})


# ==========================
#   ДОПОЛНИТЕЛЬНЫЕ РОУТЫ
# ==========================
@app.route("/send_sms", methods=["POST"])
def send_sms():
    """Эндпоинт для отправки SMS через устройство"""
    data = request.json
    phone = data.get("phone")
    message = data.get("message")
    
    print(f"[SMS] Would send to {phone}: {message}")
    
    return jsonify({"status": "OK", "message": "SMS queued"})


@app.route("/get")
def get_data():
    return jsonify({"data": last_data, "auto_mode": auto_mode})


@app.route("/")
def home():
    return "GPS Tracker Server"


# ====================
#   Запуск потоков
# ====================
if __name__ == "__main__":
    auto_thread = threading.Thread(target=auto_send, daemon=True)
    auto_thread.start()
    
    telegram_thread = threading.Thread(target=bot.infinity_polling, daemon=True)
    telegram_thread.start()
    
    app.run(host="0.0.0.0", port=5000, debug=False, use_reloader=False)