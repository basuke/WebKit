const localhost = 'localhost';
const loopbackAddress = '127.0.0.1';
const host = window.location.hostname;
const port = window.location.port;

const pathnameComponents = window.location.pathname.split('/');
const inResources = pathnameComponents.includes("resources");
const basePath = pathnameComponents.splice(0, pathnameComponents.length - (inResources ? 2 : 1)).join('/');
const otherHost = host != localhost ? localhost : loopbackAddress;

const bgColor = host != localhost ? 'lightblue' : 'lightgreen';

window.testVars = {
    localhost,
    loopbackAddress,
    host,
    port,
    basePath,
    otherHost,

    writeSharedHtml(name) {
        document.writeln(`File: '${name}: ${location.href}`);
        console.log(`Message from '${name}: ${location.href}`);
        document.writeln(`<style> body { background-color: lch(from ${bgColor} calc(l + 40) c h); }</style>`)
    },
    otherUrl(path) {
        return `http://${otherHost}:${port}${basePath}${path}`;
    },
    addOtherIframe(path, height = 80) {
        const iframe = document.createElement('iframe');
        iframe.src = this.otherUrl(path);
        iframe.style = `width: 100%; height: ${height}px`;

        console.log(iframe);
        document.body.append(iframe);
    }
};
