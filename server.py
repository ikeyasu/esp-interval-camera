from flask import Flask
from flask import request
import os
from PIL import Image

def image_dir():
  if not os.path.exists("./images"):
    os.makedirs("./images")
  return "images"

def run_http_server(host):
  app = Flask(__name__)

  @app.route("/", methods=['GET', 'POST'])
  def index():
    if request.method == 'POST':
      print("image size: " + str(len(request.data)))
      jpg = image_dir() + '/img' + request.args['n'] + '.jpg'
      outfile = open(jpg, 'ab')
      outfile.write(request.data)
      outfile.close()
      if request.args['last'] == 'y':
        print("{} recieved.".format(jpg))
      return ''
    else:
      return ''
  app.run(host)

if __name__ == '__main__':
  run_http_server("192.168.1.22")
