<?php



/**
 * builds form
 * @param array form_state
 * @param string pid
 * @return array
 */
function current_datastreams(&$form_state, $pid) {

  module_load_include('inc', 'islandora_book', 'book_pack_utils');
  module_load_include('inc', 'fedora_repository', 'plugins/FedoraObjectDetailedContent');
  $content_helper = new FedoraObjectDetailedContent($pid);
  $objectHelper = new ObjectHelper();
  $ds_list = datastream_display_builder($content_helper->pid, $content_helper->item);
  $add_datastreams_details = get_add_datastream_data($content_helper->pid);

  $form = array();
  $form['pid'] = array(
    '#type' => 'hidden',
    '#value' => $pid,
  );

  $form['current_datastreams'] = array(
    '#type' => 'fieldset',
    '#collapsed' => true,
    '#collapsible' => true,
    '#title' => t('Current Datastreams'),
  );

  $form['current_datastreams']['current_object_details'] = array(
    '#type' => 'tabpage',
    '#title' => t('View'),
    '#content' => $ds_list,
  );
  if (count($add_datastreams_details) > 0) {
    $form['current_datastreams']['datastream_candidates'] = array(
      '#type' => 'tabpage',
      '#title' => t('View'),
      '#content' => $add_datastreams_details,
    );
  }

  return $form;
}
/**
 * builds form
 * @param array form_state
 * @param string pid
 * @return array
 */
function update_datastreams(&$form_state, $pid) {

  $collection_key = $keys[0];

  $form = array();
  $form['page_manage'] = array(
    '#type' => 'fieldset',
    '#collapsed' => true,
    '#collapsible' => true,
    '#title' => t('Update Derived Datastreams'),
    '#description' => t('Update datastreams for all pages in this book.  The stored tiffs will be used to create all derived datastreams.'),
  );
  $form['page_manage']['do_ocr'] = array(
    '#type' => 'checkbox',
    '#title' => t('Refresh OCR for this book')
  );
  $form['page_manage']['recreate_derivatives'] = array(
    '#type' => 'checkbox',
    '#title' => t('Refresh all derived images')
  );

  $form['page_manage']['make_pdf'] = array(
    '#type' => 'checkbox',
    '#title' => t('Recreate PDF from scanned text')
  );


  $form['page_manage']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Update datastreams')
  );
  return $form;
}

/**
 * update datastreams  form submit
 * @param array $form
 * @param array $form_state
 */
function update_datastreams_submit($form, &$form_state) {
  global $user;
  $book_pid = $form_state['values']['pid'];
  $item = new Fedora_Item($book_pid);

  if ($form_state['clicked_button']['#id'] == 'refresh_datastreams') {
    if ($form_state['values']['do_ocr']) {
      $do_ocr = TRUE;
    }
    if ($form_state['values']['recreate_derivatives']) {
      $refresh_images = TRUE;
    }
    if ($form_state['values']['make_pdf']) {
      $make_pdf = TRUE;
    }
    book_batch_update_pages($book_pid, $do_ocr, $refresh_images, $make_pdf);
  }

  drupal_goto('fedora/repository/' . $collection_pids[0]);
}

function book_batch_update_pages($book_pid, $do_ocr, $refresh_images, $make_pdf) {
  $page_pids = get_page_pids($book_pid);
  $count = count($page_pids);
  $batch = array(
    'title' => "updating $count page objects",
    'operations' => array(),
    'file' => drupal_get_path('module', 'islandora_book') . '/book_object_manager.inc',
  );
  if ($do_ocr || $refresh_images) {
    foreach ($page_pids as $page_pid) {
      $batch['operations'][] = array('update_individual_pages', array($page_pid, $do_ocr, $refresh_images));
    }
    if ($refresh_images) {
      $batch['operations'][] = array('replace_cover_thumbnail', array($book_pid, reset($page_pids)));
    }
  }
  if ($make_pdf) {
    $batch['operations'][] = array('recreate_pdf', array($book_pid));
  }


  batch_set($batch);
  batch_process("fedora/repository/$book_pid");
}

function update_individual_pages($page_pid, $do_ocr, $refresh_images) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'islandora_book', 'book_pack_utils');
  $item = new Fedora_Item($page_pid);
  $tiff_file_path = save_datastream_as_tempfile($page_pid, 'TIFF');
  if ($do_ocr) {
    refresh_ocr($tiff_file_path, $item);
  }
  if ($refresh_images) {
    refresh_images($tiff_file_path, $item);
  }
  file_delete($tiff_file_path);
}

function replace_cover_thumbnail($book_pid, $page_pid) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'islandora_book', 'book_pack_utils');
  $file = save_datastream_as_tempfile($page_pid, 'TN');
  $item = new fedora_item($book_pid);
  $datastreams = $item->get_datastreams_list_as_array();
  if (array_key_exists('TN', $datastreams)) {
    $item->purge_datastream('TN');
  }
  $item->add_datastream_from_file("$file", 'TN', 'Thumbnail', 'image/jpeg', 'M');
  file_delete($file);
}

function recreate_pdf($book_pid) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'islandora_book', 'book_pack_utils');
  $file_dir = save_all_tiffs_in_tempdir($book_pid, 'TIFF');
  $file_names = scandir($file_dir);
  $file_names = preg_replace('/^\.*/', '', $file_names);
  $file_names = array_values(array_filter($file_names));
  $book_item = new Fedora_Item($book_pid);
  $outputFile = "$file_dir/book.pdf";
  $tiffs = array();
  $cmdString = 'convert -compress LZW';
  foreach ($file_names as $tiff) {
    $cmdString .= " $file_dir/$tiff";
  }
  $cmdString .= " $outputFile";
  exec($cmdString, $output, $returnValue);
  $datastreams = $book_item->get_datastreams_list_as_array();
  if ($returnValue == '0') {
    if (array_key_exists('PDF', $datastreams)) {
      $book_item->purge_datastream('PDF');
    }
    $book_item->add_datastream_from_file($outputFile, 'PDF', "PDF of Book", 'application/pdf', 'M');
  }
  islandora_recursive_directory_delete($file_dir);
}
