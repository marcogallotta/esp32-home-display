FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY alembic/ alembic/
COPY alembic.ini .
COPY app/ app/
COPY config/logging.yaml config/
COPY static/ static/
COPY tools/ tools/

ENV ENV=dev

EXPOSE 443

CMD ["uvicorn", "app.wsgi:app", \
     "--host", "0.0.0.0", \
     "--ssl-keyfile", "certs/key.pem", \
     "--ssl-certfile", "certs/cert.pem", \
     "--log-config", "config/logging.yaml"]
