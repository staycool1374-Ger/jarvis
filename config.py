import os

db_password = os.environ.get('DB_PASSWORD', 'junior')

class Config:
    SQLALCHEMY_DATABASE_URI = os.environ.get('DATABASE_URL') or \
        f'mysql+pymysql://arnold:{db_password}@192.168.99.77:6603/auftragsdb'
    SQLALCHEMY_TRACK_MODIFICATIONS = False
    SECRET_KEY = os.environ.get('SECRET_KEY') or 'hasshold-secret-key-2024'
