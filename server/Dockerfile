FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY alembic/ alembic/
COPY alembic.ini .
COPY app/ app/
COPY config/logging.yaml config/app.json config/
COPY static/ static/
COPY tests/ tests/
COPY tools/ tools/
COPY conftest.py .

ENV ENV=dev

EXPOSE 8000

CMD ["uvicorn", "app.wsgi:app", \
     "--host", "0.0.0.0", \
     "--ssl-keyfile", "certs/key.pem", \
     "--ssl-certfile", "certs/cert.pem", \
     "--log-config", "config/logging.yaml"]
