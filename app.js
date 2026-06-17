const canvas = document.getElementById('bgCanvas');
const ctx = canvas.getContext('2d');

let shapes = [];

// Track the visible viewport bounds dynamically
function resizeCanvas() {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

class GeometricShape {
    constructor() {
        this.x = Math.random() * canvas.width;
        this.y = Math.random() * canvas.height;
        this.size = Math.random() * 8 + 4; 
        this.speedX = (Math.random() - 0.5) * 0.3; // Very gentle drifting speed
        this.speedY = (Math.random() - 0.5) * 0.3;
        this.type = Math.floor(Math.random() * 3); 
        this.color = this.getRandomColor();
        this.angle = Math.random() * Math.PI * 2;
        this.spin = (Math.random() - 0.5) * 0.005;
    }

    getRandomColor() {
        const colors = [
            'rgba(165, 201, 235, 0.35)', 
            'rgba(226, 240, 217, 0.45)', 
            'rgba(240, 225, 247, 0.45)', 
            'rgba(251, 234, 219, 0.45)'  
        ];
        return colors[Math.floor(Math.random() * colors.length)];
    }

    update() {
        this.x += this.speedX;
        this.y += this.speedY;
        this.angle += this.spin;

        // Wrap around bounds cleanly matching viewport size
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
        ctx.lineWidth = 1.5;

        if (this.type === 0) {
            ctx.beginPath();
            ctx.arc(0, 0, this.size / 2, 0, Math.PI * 2);
            ctx.fill();
        } else if (this.type === 1) {
            ctx.strokeRect(-this.size / 2, -this.size / 2, this.size, this.size);
        } else if (this.type === 2) {
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

function init() {
    shapes = [];
    // Adjust density to look crisp on both giant desktop setups and standard mobile widths
    const initialDensity = Math.min(Math.floor((canvas.width * canvas.height) / 22000), 60);
    for (let i = 0; i < initialDensity; i++) {
        shapes.push(new GeometricShape());
    }
}

function animate() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    shapes.forEach(shape => {
        shape.update();
        shape.draw();
    });
    requestAnimationFrame(animate);
}

// Re-initialize array size dynamically if the window scales dramatically
window.addEventListener('resize', () => {
    init();
});

init();
animate();