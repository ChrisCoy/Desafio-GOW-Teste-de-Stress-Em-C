import http from 'k6/http';
import { check } from 'k6';

export let options = {
  vus: 50,
  duration: '5s',
};

const generateRandomDate = () => {
  const year = Math.floor(Math.random() * (2023 - 1950 + 1)) + 1950;
  const month = Math.floor(Math.random() * 12) + 1;
  const day = Math.floor(Math.random() * 28) + 1;
  return `${year}-${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
};

export default function () {
  const payload = JSON.stringify({
    apelido: `Apelido-${Math.random().toString(36).substring(2, 15)}`,
    nome: `Pessoa ${Math.random().toString(36).substring(2, 15)}`,
    nascimento: generateRandomDate(),
  });

  const headers = { 'Content-Type': 'application/json' };
  const res = http.post('http://host.docker.internal:9999/programadores', payload, { headers });

  check(res, {
    'status é 201 (Created)': (r) => r.status === 201,
  });
}
