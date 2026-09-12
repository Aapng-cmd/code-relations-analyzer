const express = require("express");
const app = express();
app.use(express.static("static"));
app.get("/", (req, res) => {
    res.sendFile("templates/index.html");
});
app.listen(3000);
