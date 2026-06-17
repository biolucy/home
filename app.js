const canvas = document.getElementById('bgCanvas');
const ctx = canvas.getContext('2d');

let shapes = [];

// Resize canvas perfectly to fit window
function resizeCanvas() {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

// Shape Blueprint Class
class GeometricShape {
    constructor() {
        this.x = Math.random() * canvas.width;
        this.y = Math.random() * canvas.height;
        this.size = Math.random() * 8 + 4; 
        this.speedX = (Math.random() - 0.5) * 0.4; // Soft slow movement
        this.speedY = (Math.random() - 0.5) * 0.4;
        this.type = Math.floor(Math.random() * 3); // 0=Dot, 1=Square Outline, 2=Triangle Outline
        this.color = this.getRandomColor();
        this.angle = Math.random() * Math.PI * 2;
        this.spin = (Math.random() - 0.5) * 0.01;
    }

    getRandomColor() {
        const colors = [
            'rgba(165, 201, 235, 0.4)',  // Soft Blue
            'rgba(226, 240, 217, 0.5)',  // Soft Green
            'rgba(240, 225, 247, 0.5)',  // Soft Purple
            'rgba(251, 234, 219, 0.5)'   // Soft Orange
        ];
        return colors[Math.floor(Math.random() * colors.length)];
    }

    update() {
        this.x += this.speedX;
        this.y += this.speedY;
        this.angle += this.spin;

        // Screen wrap-around behavior
        if (this.x < -20) this.x = canvas.width + 20;
        if (this.x > canvas.width + 20) this.x = -20;
        if (this.y < -20) this.y = canvas.height + 20;
        if (this.y > canvas.height + 20) this.y = -20;
    }

    draw() {
        ctx.save();
        ctx.translate(this.x, this.y);
        ctx.rotate(this.angle);
        ctx.strokeStyle = this.color;
        ctx.fillStyle = this.color;
        ctx.lineWidth = 2;

        if (this.type === 0) {
            // Animated Dot
            ctx.beginPath();
            ctx.arc(0, 0, this.size / 2, 0, Math.PI * 2);
            ctx.fill();
        } else if (this.type === 1) {
            // Square Outline
            ctx.strokeRect(-this.size / 2, -this.size / 2, this.size, this.size);
        } else if (this.type === 2) {
            // Triangle Outline
            ctx.beginPath();
            ctx.moveTo(0, -this.size / 2);
            ctx.lineTo(this.size / 2, this.size / 2);
            ctx.lineTo(-this.size / 2, this.size / 2);
            ctx.closePath();
            ctx.stroke();
        }
        ctx.restore();
    }
}

// Populate the canvas with elements
function init() {
    shapes = [];
    const initialDensity = Math.floor((canvas.width * canvas.height) / 25000); // Scale based on screen size
    for (let i = 0; i < initialDensity; i++) {
        shapes.push(new GeometricShape());
    }
}

// Continuous Animation Loop
function animate() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    shapes.forEach(shape => {
        shape.update();
        shape.draw();
    });
    requestAnimationFrame(animate);
}

init();
animate();