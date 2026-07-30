FROM python:3.12-slim
WORKDIR /bot
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY steamtools_bot.py .
RUN mkdir -p codes
CMD ["python", "-u", "steamtools_bot.py"]
