# ♻️ Lixeira Desidratadora de Resíduos Orgânicos

Projeto de lixeira passiva para **desidratação de resíduos orgânicos** alimentada por **energia solar**. O design combina modelagem 3D (SolidWorks), impressão 3D e sensoriamento ambiental (BME) para reduzir o volume e o odor do lixo orgânico doméstico.

---

## 🎯 Objetivo

Desenvolver um sistema de baixo custo e zero consumo de energia elétrica da rede que acelera a desidratação de resíduos orgânicos através de:

- Circulação de ar forçada por convecção solar
- Estrutura impressa em 3D com grades laterais para ventilação
- Monitoramento de temperatura e umidade interna (sensor BME)

---

## 📂 Estrutura de Arquivos

| Pasta / Arquivo | Conteúdo |
|----------------|---------|
| `Modelo 3D Lixeira Passiva/` | Renders e vistas do modelo 3D finalizado |
| `Modelos/Lixeira/` | Arquivos SolidWorks (`.SLDPRT`) das peças |
| `Modelos/Lixeira impressão/` | Arquivos `.STL` e G-code para impressão 3D |
| `Documentação/Lista de Componentes.xlsx` | Lista de materiais e componentes |
| `Testes/` | Planilhas e registros dos experimentos realizados |
| `Projeto-Lixeira.pdf` | Documentação completa do projeto |

---

## 🖨️ Peças para Impressão 3D

| Peça | Arquivo |
|------|---------|
| Base | `base.gcode` / `baseL.STL` |
| Fundo da base | `fundoBase.gcode` / `fundoBaseL.STL` |
| Paredes A e C | `paredeA.gcode`, `paredeC.gcode` / `parede1e3.STL` |
| Paredes B e D | `paredeB.gcode`, `paredeD.gcode` / `parede2e4.STL` |
| Grades das paredes A e C | `grade_paredeA.gcode` / `grade_parede1e3.STL` |
| Grades das paredes B e D | `grade_paredeB.gcode` / `grade_parede2e4.STL` |
| Colunas (×4) | `colunaA–D.gcode` / `colunaL2.STL` |
| Tampa | `tampa.gcode` / `tampaL.STL` |

---

## 🛠️ Tecnologias Utilizadas

| Categoria | Tecnologia |
|----------|-----------|
| **Modelagem 3D** | SolidWorks |
| **Fabricação** | Impressão 3D (FDM) |
| **Sensoriamento** | Sensor BME (temperatura, umidade, pressão) |
| **Energia** | Painel solar passivo |

---

## 📊 Testes

Os experimentos avaliaram a eficiência de desidratação comparando:
- **Lixeira ativa** (com assistência de calor solar)
- **Lixeira passiva** (convecção natural)

Os resultados estão documentados em `Testes/Testes das Lixeiras A e P/Planilha dos testes das lixeiras.xlsx`.

---

## 📄 Documentação Completa

Consulte o arquivo [`Projeto-Lixeira.pdf`](./Projeto-Lixeira.pdf) para o relatório completo com metodologia, resultados e análises.
