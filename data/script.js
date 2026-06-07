/*
 * Guarda referências aos três elementos atualizados durante a procura por
 * redes. Como o script usa defer no HTML, o documento já estará construído
 * quando estas consultas forem executadas.
 */
const select = document.querySelector('#ssid');
const status = document.querySelector('#status');
const button = document.querySelector('#save');

/*
 * O endpoint devolve um JSON pequeno gerado pelo próprio ESP32. Usar
 * textContent e elementos DOM, em vez de innerHTML, impede que nomes de rede
 * contendo caracteres especiais sejam interpretados como código HTML.
 */
fetch('/networks')
  .then(response => {
    if (!response.ok) {
      throw new Error('Falha no scan');
    }

    return response.json();
  })
  .then(networks => {
    /* Remove a mensagem temporária "Procurando redes..." antes de listar. */
    select.textContent = '';

    if (!networks.length) {
      const option = document.createElement('option');
      option.textContent = 'Nenhuma rede encontrada';
      option.value = '';
      select.appendChild(option);
      status.textContent = 'Aproxime o sensor do roteador e recarregue a página.';
      return;
    }

    /*
     * Cada objeto contém SSID, intensidade em dBm e a indicação de rede aberta.
     * O value conserva somente o SSID que será enviado no formulário.
     */
    networks.forEach(network => {
      const option = document.createElement('option');
      option.value = network.ssid;
      const securityLabel = network.open ? ' — aberta' : '';
      option.textContent =
        `${network.ssid} (${network.rssi} dBm)${securityLabel}`;
      select.appendChild(option);
    });

    /* O formulário é liberado somente depois de existir uma opção válida. */
    select.disabled = false;
    button.disabled = false;
    status.textContent = `${networks.length} rede(s) encontrada(s).`;
  })
  .catch(() => {
    /*
     * Mantém o formulário desabilitado quando o scan falha para que uma opção
     * incompleta não seja enviada ao firmware.
     */
    status.textContent =
      'Não foi possível procurar redes. Recarregue a página para tentar novamente.';
  });
