package main

import (
	"html/template"
	"net/http"
)

func main() {
	http.Handle("/static/", http.StripPrefix("/static/", http.FileServer(http.Dir("static"))))
	tmpl := template.Must(template.ParseFiles("templates/index.html"))
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		_ = tmpl.Execute(w, nil)
	})
	_ = http.ListenAndServe(":8080", nil)
}
