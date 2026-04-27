FROM python:3.9-slim

WORKDIR /app

# Install system dependencies untuk PyAV
RUN apt-get update && apt-get install -y --no-install-recommends \
    libavformat-dev \
    libavcodec-dev \
    libavdevice-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    libavfilter-dev \
    pkg-config \
    gcc \
    libc-dev \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

# Buat folder untuk upload frame & video
RUN mkdir -p /app/static/uploads

EXPOSE 5000

CMD ["python", "app.py"]
