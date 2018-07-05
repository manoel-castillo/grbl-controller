# grbl-controller

Solução para Arduino para controlar uma CNC GRBL sem a necessidade de um PC, controlando através de outro Arduino e lendo o G-Code de um cartão SD.

## Pre-requisitos

Arduino com GRBL instalado;
Arduino Uno com LCD Keypad Shield;
Modulo SD ou MicroSD para Arduino;
Fonte de alimetção de 9v a 12v com 1A de corrente no mínimo;
Cartão DS formatado em FAT32;
IDE para Arduino (usei Arduino Studio).

## Instalação

Basta fazer checkout deste repositório e abrir na IDE. Após isso é só fazer upload pra memória Flash da placa (Arduino com LCD). Para fazer o upload via USB, desconectar os pinos Tx e Rx.

## Versão

Esta versão é um protótipo (beta), portanto, são necessárias algumas polidas e correções de bugs. 

