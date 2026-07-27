FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY onepool ./onepool
COPY config.example.yaml ./config.yaml
COPY web ./web

RUN mkdir -p /app/data
ENV PYTHONUNBUFFERED=1
EXPOSE 1911 1901 8080

CMD ["python", "-m", "onepool", "--config", "/app/config.yaml"]
