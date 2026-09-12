from flask import Flask, render_template, send_from_directory

app = Flask(__name__)


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/static/<path:name>")
def assets(name):
    return send_from_directory("static", name)
