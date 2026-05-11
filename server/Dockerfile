# Use a lightweight Python image
FROM python:3.9-slim

# Set the working directory inside the container
WORKDIR /app

# Copy the requirements and install them
RUN pip install flask flask-cors

# Copy our app code
COPY server.py .

# Expose the port Flask runs on
EXPOSE 5000

# Start the application
CMD ["python", "server.py"]