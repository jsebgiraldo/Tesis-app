Este directorio se usa como fallback cuando se monta `projects/common` externamente.
En Docker, monta `-v "$PWD/projects/common":/project/components/common` para que esta carpeta quede reemplazada por la real.
