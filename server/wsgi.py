from config import load_config
from main import create_app

app = create_app(load_config())
