# CoReAnalyzer

## Описание

Сделано для анализа легаси проектов, в которых надо понять, что на чём строится.  
Визуализирует связи между файлами и показывает, какие классы/функции/переменные были использованы. Работает только с Python проектами.

## Начало работы

### Зависимости

* CMake
* C++

### Установка

```sh
git clone https://github.com/Aapng-cmd/code-relations-analyzer.git
cd code-relations-analyzer/build/
cmake . && cmake --build .
```

### Запуск программы

```sh
./build/code-relations-analyzer
```

## История версий

* 0.1
    * Первоначальный выпуск
