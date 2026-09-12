from models import Message3

def wrap(msg_type, header_arr, data):
    msg = Message2(header_arr, data) if msg_type == 2 else Message3(header_arr, data)
    return msg
